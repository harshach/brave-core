// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/views/frame/origin_widgets/origin_calendar_service.h"

#include "base/base64.h"
#include "base/base64url.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/run_loop.h"
#include "base/task/bind_post_task.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "brave/browser/workspaces/pref_names.h"
#include "components/os_crypt/async/browser/test_utils.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/test/browser_task_environment.h"
#include "net/base/url_util.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_builder.h"
#include "net/url_request/url_request_test_util.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "services/network/test/test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

constexpr char kTokenUrl[] = "https://oauth2.googleapis.com/token";
constexpr char kConfig[] = R"({"installed":{
  "client_id":"socket-test.apps.googleusercontent.com",
  "client_secret":"desktop-test-config"}})";
constexpr char kStoredToken[] = R"({
  "client_id":"socket-test.apps.googleusercontent.com",
  "refresh_token":"test-refresh-token"})";

class OriginCalendarServiceTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(directory_.CreateUniqueTempDir());
    config_path_ = directory_.GetPath().AppendASCII("oauth.json");
    ASSERT_TRUE(base::WriteFile(config_path_, kConfig));
    prefs_.registry()->RegisterStringPref(kOriginCalendarGoogleTokenPref, "");
    encryption_ = os_crypt_async::GetTestOSCryptAsyncForTesting();
    base::test::TestFuture<scoped_refptr<os_crypt_async::Encryptor>> future;
    encryption_->GetInstance(future.GetCallback());
    encryptor_ = future.Get();
  }
  void TearDown() override {
    if (service_) {
      service_->Shutdown();
      service_.reset();
    }
  }
  void SaveToken() {
    auto encrypted = encryptor_->EncryptString(kStoredToken);
    ASSERT_TRUE(encrypted);
    prefs_.SetString(kOriginCalendarGoogleTokenPref,
                     base::Base64Encode(*encrypted));
  }
  void CreateService() {
    service_ = std::make_unique<OriginCalendarService>(
        &prefs_, config_path_, factory_.GetSafeWeakWrapper(),
        encryption_.get());
    ASSERT_TRUE(
        base::test::RunUntil([this] { return !service_->is_initializing(); }));
  }
  int RequestLoopback(const GURL& url) {
    base::test::TestFuture<int> future;
    content::GetIOThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](GURL target, base::OnceCallback<void(int)> callback) {
              auto context = net::CreateTestURLRequestContextBuilder()->Build();
              net::TestDelegate delegate;
              auto request = context->CreateRequest(
                  target, net::DEFAULT_PRIORITY, &delegate,
                  TRAFFIC_ANNOTATION_FOR_TESTS,
                  net::handles::kInvalidNetworkHandle);
              base::RunLoop loop(base::RunLoop::Type::kNestableTasksAllowed);
              delegate.set_on_complete(loop.QuitClosure());
              request->Start();
              loop.Run();
              std::move(callback).Run(request->GetResponseCode());
            },
            url, base::BindPostTaskToCurrentDefault(future.GetCallback())));
    return future.Get();
  }

  content::BrowserTaskEnvironment task_environment_{
      content::BrowserTaskEnvironment::REAL_IO_THREAD};
  base::ScopedTempDir directory_;
  base::FilePath config_path_;
  TestingPrefServiceSimple prefs_;
  network::TestURLLoaderFactory factory_;
  std::unique_ptr<os_crypt_async::OSCryptAsync> encryption_;
  scoped_refptr<os_crypt_async::Encryptor> encryptor_;
  std::unique_ptr<OriginCalendarService> service_;
};

TEST_F(OriginCalendarServiceTest,
       RestoresEncryptedConnectionAndRefreshesAccess) {
  SaveToken();
  CreateService();
  ASSERT_TRUE(service_->is_connected());
  base::test::TestFuture<OriginCalendarService::TokenResult> future;
  service_->GetAccessToken(future.GetCallback());
  factory_.WaitForRequest(GURL(kTokenUrl));
  const auto& request = factory_.GetPendingRequest(0)->request;
  EXPECT_EQ(request.method, "POST");
  EXPECT_EQ(request.credentials_mode, network::mojom::CredentialsMode::kOmit);
  EXPECT_EQ(request.redirect_mode, network::mojom::RedirectMode::kError);
  EXPECT_NE(network::GetUploadData(request).find("grant_type=refresh_token"),
            std::string::npos);
  EXPECT_EQ(request.url.spec().find("test-refresh-token"), std::string::npos);
  ASSERT_TRUE(factory_.SimulateResponseForPendingRequest(
      kTokenUrl, R"({"access_token":"short-lived-token","expires_in":3600})"));
  ASSERT_TRUE(future.Get().has_value());
  EXPECT_EQ(*future.Get(), "short-lived-token");
  base::test::TestFuture<OriginCalendarService::TokenResult> cached;
  service_->GetAccessToken(cached.GetCallback());
  ASSERT_TRUE(cached.Get().has_value());
  EXPECT_EQ(*cached.Get(), "short-lived-token");
  EXPECT_EQ(prefs_.GetString(kOriginCalendarGoogleTokenPref)
                .find("test-refresh-token"),
            std::string::npos);
}

TEST_F(OriginCalendarServiceTest, RevokedAccessRequiresReconnection) {
  SaveToken();
  CreateService();
  base::test::TestFuture<OriginCalendarService::TokenResult> future;
  service_->GetAccessToken(future.GetCallback());
  ASSERT_TRUE(factory_.SimulateResponseForPendingRequest(
      kTokenUrl, R"({"error":"invalid_grant"})", net::HTTP_BAD_REQUEST,
      network::TestURLLoaderFactory::kWaitForRequest));
  EXPECT_FALSE(future.Get().has_value());
  EXPECT_FALSE(service_->is_connected());
  EXPECT_TRUE(prefs_.GetString(kOriginCalendarGoogleTokenPref).empty());
}

TEST_F(OriginCalendarServiceTest, TransientFailurePreservesTheSavedConnection) {
  SaveToken();
  CreateService();
  const auto saved = prefs_.GetString(kOriginCalendarGoogleTokenPref);
  base::test::TestFuture<OriginCalendarService::TokenResult> future;
  service_->GetAccessToken(future.GetCallback());
  ASSERT_TRUE(factory_.SimulateResponseForPendingRequest(
      kTokenUrl, "{}", net::HTTP_SERVICE_UNAVAILABLE,
      network::TestURLLoaderFactory::kWaitForRequest));
  EXPECT_FALSE(future.Get().has_value());
  EXPECT_TRUE(service_->is_connected());
  EXPECT_EQ(prefs_.GetString(kOriginCalendarGoogleTokenPref), saved);
}

TEST_F(OriginCalendarServiceTest, SignInUsesReadOnlyScopePkceAndFreshState) {
  CreateService();
  base::test::TestFuture<const GURL&> first;
  service_->Connect(first.GetCallback());
  const auto first_url = first.Get();
  ASSERT_EQ(first_url.host(), "accounts.google.com");
  std::string value;
  ASSERT_TRUE(net::GetValueForKeyInQuery(first_url, "scope", &value));
  EXPECT_EQ(value, "https://www.googleapis.com/auth/calendar.events.readonly");
  ASSERT_TRUE(
      net::GetValueForKeyInQuery(first_url, "code_challenge_method", &value));
  EXPECT_EQ(value, "S256");
  ASSERT_TRUE(net::GetValueForKeyInQuery(first_url, "code_challenge", &value));
  EXPECT_EQ(value.size(), 43u);
  ASSERT_TRUE(net::GetValueForKeyInQuery(first_url, "redirect_uri", &value));
  const GURL redirect(value);
  EXPECT_EQ(redirect.host(), "127.0.0.1");
  EXPECT_NE(redirect.EffectiveIntPort(), 0);
  std::string first_state;
  ASSERT_TRUE(net::GetValueForKeyInQuery(first_url, "state", &first_state));
  EXPECT_EQ(first_state.size(), 43u);
  service_->CancelConnection();
  EXPECT_FALSE(service_->is_connecting());
  EXPECT_FALSE(service_->is_connected());

  base::test::TestFuture<const GURL&> second;
  service_->Connect(second.GetCallback());
  ASSERT_TRUE(net::GetValueForKeyInQuery(second.Get(), "state", &value));
  EXPECT_NE(value, first_state);
  service_->CancelConnection();
}

TEST_F(OriginCalendarServiceTest,
       DisconnectClearsLocalCredentialsAndRevokesAccess) {
  SaveToken();
  CreateService();
  ASSERT_TRUE(service_->is_connected());
  service_->Disconnect();
  EXPECT_FALSE(service_->is_connected());
  EXPECT_TRUE(prefs_.GetString(kOriginCalendarGoogleTokenPref).empty());
  factory_.WaitForRequest(GURL("https://oauth2.googleapis.com/revoke"));
  const auto& request = factory_.GetPendingRequest(0)->request;
  EXPECT_EQ(network::GetUploadData(request), "token=test-refresh-token");
}

TEST_F(OriginCalendarServiceTest,
       MissingClientConfigHasAnExplicitConnectionError) {
  config_path_ = directory_.GetPath().AppendASCII("not-configured.json");
  CreateService();
  EXPECT_FALSE(service_->is_connected());
  EXPECT_FALSE(service_->error().empty());
  EXPECT_EQ(factory_.NumPending(), 0);
}

TEST_F(OriginCalendarServiceTest, LoopbackRejectsWrongStateAndSavesConnection) {
  CreateService();
  base::test::TestFuture<const GURL&> authorization;
  service_->Connect(authorization.GetCallback());
  const GURL url = authorization.Get();
  std::string redirect;
  std::string state;
  ASSERT_TRUE(net::GetValueForKeyInQuery(url, "redirect_uri", &redirect));
  ASSERT_TRUE(net::GetValueForKeyInQuery(url, "state", &state));
  const GURL callback =
      net::AppendQueryParameter(GURL(redirect), "code", "test-code");
  EXPECT_EQ(RequestLoopback(
                net::AppendQueryParameter(callback, "state", "wrong-state")),
            net::HTTP_NOT_FOUND);
  EXPECT_TRUE(service_->is_connecting());
  EXPECT_EQ(factory_.NumPending(), 0);

  EXPECT_EQ(
      RequestLoopback(net::AppendQueryParameter(callback, "state", state)),
      net::HTTP_OK);
  factory_.WaitForRequest(GURL(kTokenUrl));
  const auto form =
      network::GetUploadData(factory_.GetPendingRequest(0)->request);
  EXPECT_NE(form.find("code_verifier="), std::string::npos);
  EXPECT_NE(form.find("grant_type=authorization_code"), std::string::npos);
  ASSERT_TRUE(factory_.SimulateResponseForPendingRequest(
      kTokenUrl, R"({"access_token":"new-access","expires_in":3600,
                      "refresh_token":"new-refresh"})"));
  ASSERT_TRUE(
      base::test::RunUntil([this] { return service_->is_connected(); }));
  const std::string saved = prefs_.GetString(kOriginCalendarGoogleTokenPref);
  EXPECT_EQ(saved.find("new-refresh"), std::string::npos);
  std::string ciphertext;
  ASSERT_TRUE(base::Base64Decode(saved, &ciphertext));
  std::string plaintext;
  ASSERT_TRUE(encryptor_->DecryptString(ciphertext, &plaintext));
  const auto token =
      base::JSONReader::ReadDict(plaintext, base::JSON_PARSE_RFC);
  ASSERT_TRUE(token);
  ASSERT_TRUE(token->FindString("refresh_token"));
  EXPECT_EQ(*token->FindString("refresh_token"), "new-refresh");
}

}  // namespace
