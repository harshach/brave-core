// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_STARTUP_ORIGIN_EXTERNAL_LINK_ROUTER_H_
#define BRAVE_BROWSER_UI_STARTUP_ORIGIN_EXTERNAL_LINK_ROUTER_H_

#include <string>

class Browser;
class BrowserWindowInterface;
class GURL;
struct NavigateParams;

namespace origin_external_link {

enum class KeepDisposition {
  kNewPage,
  kSplit,
  kReplace,
};

bool IsTemporaryLinkBrowser(const BrowserWindowInterface* browser);

// Routes an operating-system link directly to a mapped Space, or to the
// profile's single ephemeral link window when no mapping exists.
void ConfigureNavigation(const GURL& url,
                         BrowserWindowInterface* fallback_browser,
                         NavigateParams* params);

// Returns the best destination for an unassigned link: a persisted rule, a
// Space already containing the domain, or the last active Space.
std::string GetSuggestedSpaceId(Browser* temporary_browser, const GURL& url);

// Moves the active temporary page into a normal browser window. When
// `always_open_domain_here` is true, subsequent OS links for the same domain
// bypass the temporary window.
bool KeepActivePage(Browser* temporary_browser,
                    const std::string& space_id,
                    bool always_open_domain_here,
                    KeepDisposition disposition = KeepDisposition::kNewPage);

void DiscardActivePage(Browser* temporary_browser);

}  // namespace origin_external_link

#endif  // BRAVE_BROWSER_UI_STARTUP_ORIGIN_EXTERNAL_LINK_ROUTER_H_
