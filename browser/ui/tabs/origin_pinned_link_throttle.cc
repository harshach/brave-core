// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/tabs/origin_pinned_link_throttle.h"

#include <memory>
#include <string_view>

#include "brave/browser/ui/startup/origin_external_link_router.h"
#include "brave/browser/ui/tabs/origin_space_controller.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_throttle_registry.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/reload_type.h"
#include "content/public/browser/web_contents.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "net/base/url_util.h"
#include "net/http/http_request_headers.h"
#include "url/gurl.h"

namespace {

// The window of `contents` when it is a pinned page in a normal Origin window.
// The temporary link window has no Space controller, so its pages never Peek.
BrowserWindowInterface* GetPinnedPageWindow(content::WebContents* contents) {
  tabs::TabInterface* tab = tabs::TabInterface::MaybeGetFromContents(contents);
  if (!tab || !tab->IsPinned()) {
    return nullptr;
  }
  BrowserWindowInterface* browser = tab->GetBrowserWindowInterface();
  return browser && browser->GetFeatures().origin_space_controller() ? browser
                                                                     : nullptr;
}

// A sign-in returns to the page that started it, so it loads in place: in
// Peek the pinned page would never see it finish. OAuth and OpenID requests
// name a client_id or redirect_uri, and SAML ones carry a SAMLRequest.
bool IsSignInRequest(const GURL& url) {
  for (net::QueryIterator it(url); !it.IsAtEnd(); it.Advance()) {
    const std::string_view key = it.GetKey();
    if (key == "client_id" || key == "redirect_uri" || key == "SAMLRequest") {
      return true;
    }
  }
  return false;
}

}  // namespace

// static
void OriginPinnedLinkThrottle::MaybeCreateAndAdd(
    content::NavigationThrottleRegistry& registry) {
  content::NavigationHandle& handle = registry.GetNavigationHandle();
  if (handle.IsInPrimaryMainFrame() && handle.IsRendererInitiated() &&
      GetPinnedPageWindow(handle.GetWebContents())) {
    registry.AddThrottle(std::make_unique<OriginPinnedLinkThrottle>(registry));
  }
}

OriginPinnedLinkThrottle::OriginPinnedLinkThrottle(
    content::NavigationThrottleRegistry& registry)
    : content::NavigationThrottle(registry) {}

OriginPinnedLinkThrottle::~OriginPinnedLinkThrottle() = default;

content::NavigationThrottle::ThrottleCheckResult
OriginPinnedLinkThrottle::WillStartRequest() {
  content::NavigationHandle* handle = navigation_handle();
  // Only a link the user follows from the page. Without an initiator origin
  // the Peek would load as a typed address, sending SameSite=Strict cookies
  // and `Sec-Fetch-Site: none`. Only GET carries everything in the URL.
  if (!handle->HasUserGesture() || !handle->GetInitiatorOrigin() ||
      handle->GetRequestMethod() != net::HttpRequestHeaders::kGetMethod ||
      handle->IsSameDocument() || handle->IsDownload() || handle->IsHistory() ||
      handle->GetReloadType() != content::ReloadType::NONE) {
    return PROCEED;
  }

  content::WebContents* contents = handle->GetWebContents();
  BrowserWindowInterface* browser = GetPinnedPageWindow(contents);
  if (!browser) {
    return PROCEED;
  }
  // Compare with where the page was pinned, not where it is, so a link back
  // home from a page that wandered off still loads in place.
  GURL home =
      browser->GetFeatures().origin_space_controller()->GetPinnedHomeUrl(
          contents);
  if (!home.is_valid()) {
    home = contents->GetLastCommittedURL();
  }
  const GURL& url = handle->GetURL();
  if (!home.SchemeIsHTTPOrHTTPS() || !url.SchemeIsHTTPOrHTTPS() ||
      net::registry_controlled_domains::SameDomainOrHost(
          home, url,
          net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES) ||
      IsSignInRequest(url)) {
    return PROCEED;
  }

  content::OpenURLParams params =
      content::OpenURLParams::FromNavigationHandle(handle);
  params.initiator_frame_token = handle->GetInitiatorFrameToken();
  params.initiator_process_id =
      handle->GetInitiatorProcessId().GetUnsafeValue();
  return origin_external_link::OpenInTemporaryWindow(browser, params)
             ? CANCEL_AND_IGNORE
             : PROCEED;
}

const char* OriginPinnedLinkThrottle::GetNameForLogging() {
  return "OriginPinnedLinkThrottle";
}
