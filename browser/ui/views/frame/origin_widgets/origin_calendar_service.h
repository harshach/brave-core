// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_WIDGETS_ORIGIN_CALENDAR_SERVICE_H_
#define BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_WIDGETS_ORIGIN_CALENDAR_SERVICE_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/sequence_bound.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "components/keyed_service/core/keyed_service.h"
#include "url/gurl.h"

class PrefService;
class Profile;
namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network
namespace os_crypt_async {
class Encryptor;
class OSCryptAsync;
}  // namespace os_crypt_async

// One read-only Google Calendar connection per regular profile. OAuth runs in
// a normal browser tab with PKCE and a temporary loopback redirect listener.
// Only an OS-encrypted refresh token persists; access tokens stay in memory.
class OriginCalendarService : public KeyedService {
 public:
  using TokenResult = base::expected<std::string, std::u16string>;
  using TokenCallback = base::OnceCallback<void(TokenResult)>;
  using OpenUrlCallback = base::OnceCallback<void(const GURL&)>;

  static OriginCalendarService* GetForProfile(Profile* profile);

  OriginCalendarService(
      PrefService* prefs,
      base::FilePath client_config_path,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      os_crypt_async::OSCryptAsync* encryption_service);
  ~OriginCalendarService() override;
  void Shutdown() override;

  bool is_initializing() const { return initializing_; }
  bool is_connecting() const { return connecting_; }
  bool is_connected() const { return !refresh_token_.empty(); }
  const std::u16string& error() const { return error_; }
  base::CallbackListSubscription Subscribe(base::RepeatingClosure on_changed);

  void Connect(OpenUrlCallback open_url);
  void CancelConnection();
  void Disconnect();
  void GetAccessToken(TokenCallback callback);
  void InvalidateAccessToken();

 private:
  class LoopbackServer;
  void LoadConfiguration(OpenUrlCallback open_url);
  void OnConfiguration(OpenUrlCallback open_url,
                       std::optional<base::DictValue> config);
  void OnEncryptor(OpenUrlCallback open_url,
                   scoped_refptr<os_crypt_async::Encryptor> encryptor);
  void StartConnection(OpenUrlCallback open_url);
  void OnListenerReady(OpenUrlCallback open_url, GURL redirect_uri);
  void OnAuthorizationCode(TokenResult code);
  void RequestToken(std::string form, bool authorizing);
  void OnTokenResponse(bool authorizing, std::optional<std::string> body);
  void FinishTokens(TokenResult result);
  void OnConnectionTimeout();
  void OnRevoked(std::optional<std::string> body);
  void NotifyChanged();

  const raw_ptr<PrefService> prefs_;
  const base::FilePath client_config_path_;
  const scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  const raw_ptr<os_crypt_async::OSCryptAsync> encryption_service_;
  scoped_refptr<os_crypt_async::Encryptor> encryptor_;
  std::string client_id_;
  std::string client_secret_;
  std::string refresh_token_;
  std::string access_token_;
  base::Time token_expiry_;
  bool initializing_ = true;
  bool connecting_ = false;
  std::u16string error_;
  std::string verifier_;
  std::string state_;
  GURL redirect_uri_;
  base::SequenceBound<LoopbackServer> loopback_;
  base::OneShotTimer connection_timeout_;
  std::unique_ptr<network::SimpleURLLoader> token_loader_;
  std::unique_ptr<network::SimpleURLLoader> revocation_loader_;
  std::vector<TokenCallback> token_callbacks_;
  base::RepeatingClosureList observers_;
  base::WeakPtrFactory<OriginCalendarService> weak_factory_{this};
  base::WeakPtrFactory<OriginCalendarService> flow_weak_factory_{this};
};

#endif  // BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_WIDGETS_ORIGIN_CALENDAR_SERVICE_H_
