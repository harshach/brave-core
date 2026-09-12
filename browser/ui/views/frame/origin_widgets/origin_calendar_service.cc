// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/views/frame/origin_widgets/origin_calendar_service.h"

#include <utility>

#include "base/base64.h"
#include "base/base64url.h"
#include "base/byte_size.h"
#include "base/command_line.h"
#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/no_destructor.h"
#include "base/rand_util.h"
#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/task/bind_post_task.h"
#include "base/task/thread_pool.h"
#include "brave/browser/workspaces/pref_names.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"
#include "chrome/browser/profiles/profile_selections.h"
#include "components/os_crypt/async/browser/os_crypt_async.h"
#include "components/os_crypt/async/common/encryptor.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/storage_partition.h"
#include "crypto/sha2.h"
#include "net/base/ip_address.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/base/url_util.h"
#include "net/http/http_response_headers.h"
#include "net/log/net_log_source.h"
#include "net/server/http_server.h"
#include "net/server/http_server_request_info.h"
#include "net/server/http_server_response_info.h"
#include "net/socket/tcp_server_socket.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

namespace {

constexpr char kCalendarScope[] =
    "https://www.googleapis.com/auth/calendar.events.readonly";
constexpr auto kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("origin_google_calendar_oauth", R"(
      semantics {
        sender: "Origin Google Calendar Connection"
        description:
          "Exchanges the user's Google Calendar authorization code for tokens, "
          "refreshes access tokens, or revokes access on disconnection. A local "
          "loopback listener receives the user's authorization response."
        trigger: "The user chooses Connect Google Calendar or Disconnect, or "
                 "an existing connection needs a fresh access token."
        data: "An OAuth client ID, desktop client secret, authorization code "
              "and PKCE verifier, or a refresh token."
        destination: GOOGLE_OWNED_SERVICE
      }
      policy {
        cookies_allowed: NO
        setting: "Disconnect Google Calendar from its widget."
        policy_exception_justification: "Not implemented."
      })");

std::string Form(const base::StringPairs& values) {
  std::string form;
  for (const auto& [name, value] : values) {
    if (!form.empty()) {
      form += '&';
    }
    form += base::EscapeUrlEncodedData(name, true) + "=" +
            base::EscapeUrlEncodedData(value, true);
  }
  return form;
}

std::string RandomToken() {
  std::string token;
  base::Base64UrlEncode(base::RandBytesAsString(32),
                        base::Base64UrlEncodePolicy::OMIT_PADDING, &token);
  return token;
}

std::optional<base::DictValue> ReadConfiguration(const base::FilePath& path) {
  std::string data;
  if (!base::ReadFileToStringWithMaxSize(path, &data, 64 * 1024)) {
    return std::nullopt;
  }
  return base::JSONReader::ReadDict(data, base::JSON_PARSE_RFC);
}

std::unique_ptr<network::SimpleURLLoader> PostRequest(const GURL& url,
                                                      std::string form) {
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->method = "POST";
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->redirect_mode = network::mojom::RedirectMode::kError;
  auto loader =
      network::SimpleURLLoader::Create(std::move(request), kTrafficAnnotation);
  loader->SetTimeoutDuration(base::Seconds(20));
  loader->SetAllowHttpErrorResults(true);
  loader->AttachStringForUpload(form, "application/x-www-form-urlencoded");
  return loader;
}

class CalendarServiceFactory : public ProfileKeyedServiceFactory {
 public:
  CalendarServiceFactory()
      : ProfileKeyedServiceFactory(
            "OriginCalendarService",
            ProfileSelections::BuildForRegularProfile()) {}
  static CalendarServiceFactory* GetInstance() {
    static base::NoDestructor<CalendarServiceFactory> factory;
    return factory.get();
  }
  OriginCalendarService* GetForProfile(Profile* profile) {
    return static_cast<OriginCalendarService*>(
        GetServiceForBrowserContext(profile, true));
  }

 private:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override {
    auto* profile = Profile::FromBrowserContext(context);
    const auto* command_line = base::CommandLine::ForCurrentProcess();
    auto path =
        command_line->GetSwitchValuePath("origin-google-calendar-oauth-config");
    if (path.empty()) {
      path = profile->GetPath().DirName().AppendASCII(
          "google-calendar-oauth.json");
    }
    return std::make_unique<OriginCalendarService>(
        profile->GetPrefs(), path,
        profile->GetDefaultStoragePartition()
            ->GetURLLoaderFactoryForBrowserProcess(),
        g_browser_process ? g_browser_process->os_crypt_async() : nullptr);
  }
};

}  // namespace

// Lives on the browser IO thread. Only a state-matched request to the bound
// loopback endpoint can finish authorization; unrelated requests are rejected.
class OriginCalendarService::LoopbackServer : public net::HttpServer::Delegate {
 public:
  LoopbackServer(std::string state,
                 base::OnceCallback<void(GURL)> ready,
                 TokenCallback code_callback)
      : state_(std::move(state)), code_callback_(std::move(code_callback)) {
    auto socket =
        std::make_unique<net::TCPServerSocket>(nullptr, net::NetLogSource());
    if (socket->Listen(net::IPEndPoint(net::IPAddress::IPv4Localhost(), 0), 4,
                       std::nullopt) != net::OK) {
      std::move(ready).Run(GURL());
      return;
    }
    server_ = std::make_unique<net::HttpServer>(std::move(socket), this);
    net::IPEndPoint address;
    if (server_->GetLocalAddress(&address) != net::OK) {
      std::move(ready).Run(GURL());
      return;
    }
    redirect_uri_ = GURL("http://" + address.ToString() + "/");
    std::move(ready).Run(redirect_uri_);
  }
  ~LoopbackServer() override = default;

 private:
  void OnConnect(int connection_id) override {
    server_->SetReceiveBufferSize(connection_id, 16 * 1024);
  }
  void OnHttpRequest(int connection_id,
                     const net::HttpServerRequestInfo& request) override {
    if (!request.path.starts_with("/?")) {
      server_->Send404(connection_id, kTrafficAnnotation);
      return;
    }
    const GURL url(redirect_uri_.spec() + request.path.substr(1));
    std::string state;
    std::string code;
    const auto host = request.headers.find("host");
    if (!code_callback_ || request.method != "GET" || url.path() != "/" ||
        host == request.headers.end() ||
        host->second !=
            base::StrCat({redirect_uri_.host(), ":", redirect_uri_.port()}) ||
        !net::GetValueForKeyInQuery(url, "state", &state) || state != state_) {
      server_->Send404(connection_id, kTrafficAnnotation);
      return;
    }
    const bool granted = net::GetValueForKeyInQuery(url, "code", &code) &&
                         !code.empty() && code.size() < 4096;
    net::HttpServerResponseInfo response;
    response.AddHeader("Cache-Control", "no-store");
    response.AddHeader("Content-Security-Policy", "default-src 'none'");
    response.AddHeader("Referrer-Policy", "no-referrer");
    response.SetBody(
        granted ? "<!doctype html><title>Google Calendar</title><h1>Return to "
                  "Socket</h1><p>Your calendar connection is being completed. "
                  "You can close this tab.</p>"
                : "<!doctype html><title>Google Calendar</title><h1>Calendar "
                  "was not connected</h1><p>Return to Socket to try again.</p>",
        "text/html; charset=utf-8");
    server_->SendResponse(connection_id, response, kTrafficAnnotation);
    if (granted) {
      std::move(code_callback_).Run(std::move(code));
    } else {
      std::move(code_callback_)
          .Run(base::unexpected(
              std::u16string(u"Google Calendar access wasn't granted. Try "
                             u"connecting again.")));
    }
  }
  void OnWebSocketRequest(int connection_id,
                          const net::HttpServerRequestInfo&) override {
    server_->Close(connection_id);
  }
  void OnWebSocketMessage(int, std::string) override {}
  void OnClose(int) override {}

  std::string state_;
  TokenCallback code_callback_;
  GURL redirect_uri_;
  std::unique_ptr<net::HttpServer> server_;
};

// static
OriginCalendarService* OriginCalendarService::GetForProfile(Profile* profile) {
  if (!profile || profile->IsOffTheRecord()) {
    return nullptr;
  }
  return CalendarServiceFactory::GetInstance()->GetForProfile(profile);
}

OriginCalendarService::OriginCalendarService(
    PrefService* prefs,
    base::FilePath client_config_path,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    os_crypt_async::OSCryptAsync* encryption_service)
    : prefs_(prefs),
      client_config_path_(std::move(client_config_path)),
      url_loader_factory_(std::move(url_loader_factory)),
      encryption_service_(encryption_service) {
  LoadConfiguration(OpenUrlCallback());
}
OriginCalendarService::~OriginCalendarService() = default;

void OriginCalendarService::Shutdown() {
  weak_factory_.InvalidateWeakPtrs();
  flow_weak_factory_.InvalidateWeakPtrs();
  connection_timeout_.Stop();
  loopback_.Reset();
  token_loader_.reset();
  revocation_loader_.reset();
  token_callbacks_.clear();
  refresh_token_.clear();
  access_token_.clear();
}

base::CallbackListSubscription OriginCalendarService::Subscribe(
    base::RepeatingClosure on_changed) {
  return observers_.Add(std::move(on_changed));
}

void OriginCalendarService::LoadConfiguration(OpenUrlCallback open_url) {
  initializing_ = true;
  error_.clear();
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&ReadConfiguration, client_config_path_),
      base::BindOnce(&OriginCalendarService::OnConfiguration,
                     weak_factory_.GetWeakPtr(), std::move(open_url)));
  NotifyChanged();
}

void OriginCalendarService::OnConfiguration(
    OpenUrlCallback open_url,
    std::optional<base::DictValue> config) {
  const auto* installed = config ? config->FindDict("installed") : nullptr;
  const auto* id = installed ? installed->FindString("client_id") : nullptr;
  const auto* secret =
      installed ? installed->FindString("client_secret") : nullptr;
  if (!id || !id->ends_with(".apps.googleusercontent.com") || !secret ||
      secret->empty() || !encryption_service_) {
    initializing_ = false;
    error_ = u"Google Calendar isn't available in this build yet.";
    FinishTokens(base::unexpected(error_));
    NotifyChanged();
    return;
  }
  client_id_ = *id;
  client_secret_ = *secret;
  encryption_service_->GetInstance(
      base::BindOnce(&OriginCalendarService::OnEncryptor,
                     weak_factory_.GetWeakPtr(), std::move(open_url)));
}

void OriginCalendarService::OnEncryptor(
    OpenUrlCallback open_url,
    scoped_refptr<os_crypt_async::Encryptor> encryptor) {
  encryptor_ = std::move(encryptor);
  initializing_ = false;
  if (!encryptor_ || !encryptor_->IsEncryptionAvailable()) {
    error_ = u"The calendar connection couldn't be saved securely. Try again.";
    FinishTokens(base::unexpected(error_));
    NotifyChanged();
    return;
  }
  const std::string& encoded =
      prefs_->GetString(kOriginCalendarGoogleTokenPref);
  std::string encrypted;
  if (!encoded.empty() && base::Base64Decode(encoded, &encrypted)) {
    if (auto token = encryptor_->DecryptData(base::as_byte_span(encrypted))) {
      auto value = base::JSONReader::ReadDict(*token, base::JSON_PARSE_RFC);
      if (value && value->FindString("client_id") &&
          *value->FindString("client_id") == client_id_) {
        if (const auto* refresh = value->FindString("refresh_token")) {
          refresh_token_ = *refresh;
        }
      }
    }
    if (refresh_token_.empty()) {
      error_ = u"Connect Google Calendar again to resume syncing.";
    }
  }
  if (open_url && !is_connected()) {
    StartConnection(std::move(open_url));
  } else {
    auto callbacks = std::move(token_callbacks_);
    for (auto& callback : callbacks) {
      GetAccessToken(std::move(callback));
    }
    NotifyChanged();
  }
}

void OriginCalendarService::Connect(OpenUrlCallback open_url) {
  if (!initializing_ && !connecting_) {
    LoadConfiguration(std::move(open_url));
  }
}

void OriginCalendarService::StartConnection(OpenUrlCallback open_url) {
  error_.clear();
  connecting_ = true;
  verifier_ = RandomToken();
  state_ = RandomToken();
  loopback_ = base::SequenceBound<LoopbackServer>(
      content::GetIOThreadTaskRunner({}), state_,
      base::BindPostTaskToCurrentDefault(
          base::BindOnce(&OriginCalendarService::OnListenerReady,
                         flow_weak_factory_.GetWeakPtr(), std::move(open_url))),
      base::BindPostTaskToCurrentDefault(
          base::BindOnce(&OriginCalendarService::OnAuthorizationCode,
                         flow_weak_factory_.GetWeakPtr())));
  connection_timeout_.Start(
      FROM_HERE, base::Minutes(5),
      base::BindOnce(&OriginCalendarService::OnConnectionTimeout,
                     base::Unretained(this)));
  NotifyChanged();
}

void OriginCalendarService::OnListenerReady(OpenUrlCallback open_url,
                                            GURL redirect_uri) {
  if (!redirect_uri.is_valid()) {
    CancelConnection();
    error_ = u"Couldn't start Google sign-in. Try again.";
    NotifyChanged();
    return;
  }
  redirect_uri_ = std::move(redirect_uri);
  std::string challenge;
  base::Base64UrlEncode(crypto::SHA256HashString(verifier_),
                        base::Base64UrlEncodePolicy::OMIT_PADDING, &challenge);
  GURL url("https://accounts.google.com/o/oauth2/v2/auth");
  for (const auto& [key, value] :
       base::StringPairs{{"client_id", client_id_},
                         {"redirect_uri", redirect_uri_.spec()},
                         {"response_type", "code"},
                         {"scope", kCalendarScope},
                         {"state", state_},
                         {"code_challenge", challenge},
                         {"code_challenge_method", "S256"},
                         {"access_type", "offline"},
                         {"prompt", "consent select_account"}}) {
    url = net::AppendQueryParameter(url, key, value);
  }
  std::move(open_url).Run(url);
}

void OriginCalendarService::OnAuthorizationCode(TokenResult code) {
  if (!code.has_value()) {
    CancelConnection();
    error_ = std::move(code.error());
    NotifyChanged();
    return;
  }
  RequestToken(Form({{"grant_type", "authorization_code"},
                     {"code", *code},
                     {"client_id", client_id_},
                     {"client_secret", client_secret_},
                     {"redirect_uri", redirect_uri_.spec()},
                     {"code_verifier", verifier_}}),
               true);
}

void OriginCalendarService::CancelConnection() {
  flow_weak_factory_.InvalidateWeakPtrs();
  connecting_ = false;
  connection_timeout_.Stop();
  loopback_.Reset();
  verifier_.clear();
  state_.clear();
  token_loader_.reset();
  FinishTokens(
      base::unexpected(std::u16string(u"Calendar connection cancelled.")));
  NotifyChanged();
}

void OriginCalendarService::OnConnectionTimeout() {
  CancelConnection();
  error_ = u"Google sign-in timed out. Try connecting again.";
  NotifyChanged();
}

void OriginCalendarService::GetAccessToken(TokenCallback callback) {
  if (initializing_) {
    token_callbacks_.push_back(std::move(callback));
    return;
  }
  if (!is_connected()) {
    std::move(callback).Run(base::unexpected(
        std::u16string(u"Connect Google Calendar to see your events.")));
    return;
  }
  if (!access_token_.empty() &&
      token_expiry_ > base::Time::Now() + base::Minutes(1)) {
    std::move(callback).Run(access_token_);
    return;
  }
  token_callbacks_.push_back(std::move(callback));
  if (!token_loader_) {
    RequestToken(Form({{"grant_type", "refresh_token"},
                       {"refresh_token", refresh_token_},
                       {"client_id", client_id_},
                       {"client_secret", client_secret_}}),
                 false);
  }
}

void OriginCalendarService::InvalidateAccessToken() {
  access_token_.clear();
  token_expiry_ = base::Time();
}

void OriginCalendarService::RequestToken(std::string form, bool authorizing) {
  token_loader_ =
      PostRequest(GURL("https://oauth2.googleapis.com/token"), std::move(form));
  token_loader_->DownloadToString(
      url_loader_factory_.get(),
      base::BindOnce(&OriginCalendarService::OnTokenResponse,
                     flow_weak_factory_.GetWeakPtr(), authorizing),
      base::KiBU(64).InBytes());
}

void OriginCalendarService::OnTokenResponse(bool authorizing,
                                            std::optional<std::string> body) {
  int status = 0;
  if (token_loader_->ResponseInfo() && token_loader_->ResponseInfo()->headers) {
    status = token_loader_->ResponseInfo()->headers->response_code();
  }
  token_loader_.reset();
  connection_timeout_.Stop();
  loopback_.Reset();
  verifier_.clear();
  state_.clear();
  connecting_ = false;
  auto value = body ? base::JSONReader::ReadDict(*body, base::JSON_PARSE_RFC)
                    : std::nullopt;
  const auto* access = value ? value->FindString("access_token") : nullptr;
  const int expires = value ? value->FindInt("expires_in").value_or(0) : 0;
  if (status != 200 || !access || access->empty() || expires <= 0 ||
      access->find_first_of("\r\n") != std::string::npos ||
      access->find('\0') != std::string::npos) {
    if (value && value->FindString("error") &&
        *value->FindString("error") == "invalid_grant") {
      refresh_token_.clear();
      prefs_->ClearPref(kOriginCalendarGoogleTokenPref);
      error_ =
          u"Google Calendar access expired. Connect again to resume syncing.";
    } else {
      error_ =
          u"Couldn't connect to Google Calendar. Check your connection "
          u"and retry.";
    }
    InvalidateAccessToken();
    FinishTokens(base::unexpected(error_));
    NotifyChanged();
    return;
  }
  if (const auto* refresh = value->FindString("refresh_token")) {
    base::DictValue token;
    token.Set("client_id", client_id_);
    token.Set("refresh_token", *refresh);
    const auto serialized = base::WriteJson(token);
    const auto encrypted =
        serialized ? encryptor_->EncryptString(*serialized) : std::nullopt;
    if (!encrypted || refresh->empty()) {
      error_ =
          u"The calendar connection couldn't be saved securely. Try again.";
      FinishTokens(base::unexpected(error_));
      NotifyChanged();
      return;
    }
    refresh_token_ = *refresh;
    prefs_->SetString(kOriginCalendarGoogleTokenPref,
                      base::Base64Encode(*encrypted));
  }
  if (authorizing && refresh_token_.empty()) {
    error_ =
        u"Google Calendar wasn't connected. Try again and allow calendar "
        u"access.";
    FinishTokens(base::unexpected(error_));
    NotifyChanged();
    return;
  }
  access_token_ = *access;
  token_expiry_ = base::Time::Now() + base::Seconds(expires);
  error_.clear();
  FinishTokens(access_token_);
  NotifyChanged();
}

void OriginCalendarService::FinishTokens(TokenResult result) {
  auto callbacks = std::move(token_callbacks_);
  for (auto& callback : callbacks) {
    std::move(callback).Run(result);
  }
}

void OriginCalendarService::Disconnect() {
  std::string token = refresh_token_;
  refresh_token_.clear();
  InvalidateAccessToken();
  prefs_->ClearPref(kOriginCalendarGoogleTokenPref);
  CancelConnection();
  error_.clear();
  if (!token.empty()) {
    revocation_loader_ = PostRequest(
        GURL("https://oauth2.googleapis.com/revoke"), Form({{"token", token}}));
    revocation_loader_->DownloadToString(
        url_loader_factory_.get(),
        base::BindOnce(&OriginCalendarService::OnRevoked,
                       weak_factory_.GetWeakPtr()),
        base::KiBU(64).InBytes());
  }
  NotifyChanged();
}

void OriginCalendarService::OnRevoked(std::optional<std::string> body) {
  revocation_loader_.reset();
}

void OriginCalendarService::NotifyChanged() {
  observers_.Notify();
}
