// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/components/brave_origin/buildflags/buildflags.h"

#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)

#include <memory>
#include <string>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/test/run_until.h"
#include "brave/browser/ui/startup/origin_external_link_router.h"
#include "brave/browser/ui/tabs/origin_space_controller.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "url/gurl.h"

namespace {

std::string GetHeader(const net::test_server::HttpRequest& request,
                      const std::string& name) {
  const auto it = request.headers.find(name);
  return it == request.headers.end() ? std::string() : it->second;
}

// Shows the cookies and fetch site that tell a link from a typed address.
std::unique_ptr<net::test_server::HttpResponse> EchoRequestContext(
    const net::test_server::HttpRequest& request) {
  if (request.relative_url != "/echo") {
    return nullptr;
  }
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("text/plain");
  response->set_content(
      base::StrCat({"cookie=", GetHeader(request, "Cookie"),
                    " site=", GetHeader(request, "Sec-Fetch-Site")}));
  return response;
}

}  // namespace

class OriginPinnedLinkThrottleBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
    embedded_https_test_server().RegisterRequestHandler(
        base::BindRepeating(&EchoRequestContext));
    // Valid for a.test and b.test, so neither page is a certificate error.
    embedded_https_test_server().SetSSLConfig(
        net::EmbeddedTestServer::CERT_TEST_NAMES);
    ASSERT_TRUE(embedded_https_test_server().Start());
  }

 protected:
  // Loads `url` in the active page, then pins it.
  content::WebContents* PinActivePageAt(const GURL& url) {
    TabStripModel* model = browser()->tab_strip_model();
    content::WebContents* contents = model->GetActiveWebContents();
    EXPECT_TRUE(content::NavigateToURL(contents, url));
    model->SetTabPinned(model->GetIndexOfWebContents(contents), true);
    return contents;
  }

  // Follows a new link to `url`. ExecJs gives it a user gesture, as a click
  // would.
  void ClickLink(content::WebContents* contents, const GURL& url) {
    constexpr char kScript[] = R"(
      const link = document.createElement('a');
      link.href = $1;
      document.body.appendChild(link);
      link.click();
    )";
    ASSERT_TRUE(content::ExecJs(contents, content::JsReplace(kScript, url)));
  }

  BrowserWindowInterface* FindTemporaryLinkWindow() {
    for (BrowserWindowInterface* window : GetAllBrowserWindowInterfaces()) {
      if (origin_external_link::IsTemporaryLinkBrowser(window) &&
          !window->IsDeleteScheduled()) {
        return window;
      }
    }
    return nullptr;
  }

  // Returns the temporary link window's page once it has loaded.
  content::WebContents* WaitForPeek() {
    BrowserWindowInterface* window = nullptr;
    EXPECT_TRUE(base::test::RunUntil([&] {
      window = FindTemporaryLinkWindow();
      return window != nullptr;
    }));
    if (!window) {
      return nullptr;
    }
    content::WebContents* contents =
        window->GetTabStripModel()->GetActiveWebContents();
    EXPECT_TRUE(content::WaitForLoadStop(contents));
    return contents;
  }
};

IN_PROC_BROWSER_TEST_F(OriginPinnedLinkThrottleBrowserTest,
                       OriginPinnedPageOpensOtherSitesInPeek) {
  const GURL home = embedded_test_server()->GetURL("a.test", "/title1.html");
  const GURL same_site =
      embedded_test_server()->GetURL("a.test", "/title2.html");
  const GURL other_site =
      embedded_test_server()->GetURL("b.test", "/title3.html");
  content::WebContents* pinned = PinActivePageAt(home);
  ASSERT_EQ(
      home,
      browser()->GetFeatures().origin_space_controller()->GetPinnedHomeUrl(
          pinned));

  // A link to another site opens in front, in the temporary link window, and
  // the pinned page stays where it is.
  content::TestNavigationManager leave_home(pinned, other_site);
  ClickLink(pinned, other_site);
  ASSERT_TRUE(leave_home.WaitForNavigationFinished());
  EXPECT_FALSE(leave_home.was_committed());
  content::WebContents* peek = WaitForPeek();
  ASSERT_TRUE(peek);
  EXPECT_EQ(other_site, peek->GetLastCommittedURL());
  EXPECT_EQ(home, pinned->GetLastCommittedURL());
  EXPECT_EQ(home, pinned->GetVisibleURL());
  TabStripModel* peek_pages = FindTemporaryLinkWindow()->GetTabStripModel();
  EXPECT_EQ(1, peek_pages->count());

  // A link within the site loads in the pinned page as usual.
  content::TestNavigationObserver same_site_load(pinned);
  ClickLink(pinned, same_site);
  same_site_load.Wait();
  EXPECT_EQ(same_site, pinned->GetLastCommittedURL());
  EXPECT_EQ(1, peek_pages->count());

  // A sign-in loads in place, so it can come back to the pinned page.
  const GURL sign_in = embedded_test_server()->GetURL(
      "b.test", "/title3.html?client_id=app&redirect_uri=https%3A%2F%2Fa.test");
  content::TestNavigationObserver sign_in_load(pinned);
  ClickLink(pinned, sign_in);
  sign_in_load.Wait();
  EXPECT_EQ(sign_in, pinned->GetLastCommittedURL());
  EXPECT_EQ(1, peek_pages->count());

  // An unpinned page follows a link to another site itself.
  content::WebContents* unpinned =
      chrome::AddAndReturnTabAt(browser(), home, -1, /*foreground=*/true);
  ASSERT_TRUE(unpinned);
  ASSERT_TRUE(content::WaitForLoadStop(unpinned));
  content::TestNavigationObserver other_site_load(unpinned);
  ClickLink(unpinned, other_site);
  other_site_load.Wait();
  EXPECT_EQ(other_site, unpinned->GetLastCommittedURL());
  EXPECT_EQ(1, peek_pages->count());
}

// A Peek loads as the link it was. As a typed address it would send
// SameSite=Strict cookies and `Sec-Fetch-Site: none` to the other site.
IN_PROC_BROWSER_TEST_F(OriginPinnedLinkThrottleBrowserTest,
                       OriginPeekKeepsTheLinksInitiator) {
  const GURL echo = embedded_https_test_server().GetURL("b.test", "/echo");
  ASSERT_TRUE(content::SetCookie(browser()->GetProfile(), echo,
                                 "strict=1; SameSite=Strict; Path=/"));
  ASSERT_TRUE(content::SetCookie(browser()->GetProfile(), echo,
                                 "lax=1; SameSite=Lax; Path=/"));
  content::WebContents* pinned = PinActivePageAt(
      embedded_https_test_server().GetURL("a.test", "/title1.html"));

  ClickLink(pinned, echo);
  content::WebContents* peek = WaitForPeek();
  ASSERT_TRUE(peek);
  ASSERT_EQ(echo, peek->GetLastCommittedURL());
  const std::string request =
      content::EvalJs(peek, "document.body.textContent").ExtractString();
  EXPECT_THAT(request, testing::Not(testing::HasSubstr("strict=1")));
  EXPECT_THAT(request, testing::HasSubstr("lax=1"));
  EXPECT_THAT(request, testing::HasSubstr("site=cross-site"));
}

#endif  // BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
