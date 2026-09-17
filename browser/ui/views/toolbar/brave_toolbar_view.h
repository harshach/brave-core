/* Copyright (c) 2019 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_UI_VIEWS_TOOLBAR_BRAVE_TOOLBAR_VIEW_H_
#define BRAVE_BROWSER_UI_VIEWS_TOOLBAR_BRAVE_TOOLBAR_VIEW_H_

#include <memory>
#include <optional>

#include "base/gtest_prod_util.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "brave/components/ai_chat/core/common/buildflags/buildflags.h"
#include "brave/components/brave_vpn/common/buildflags/buildflags.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "components/prefs/pref_member.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view_targeter_delegate.h"

#if BUILDFLAG(ENABLE_AI_CHAT)
class AIChatButton;
#endif

#if BUILDFLAG(ENABLE_BRAVE_VPN)
class BraveVPNButton;
#endif

class BraveBookmarkButton;
class BraveShieldsToolbarButton;
class ScreenshotButton;
class SidePanelButton;
class TabStripComboButton;
class ToolbarButton;
class WalletButton;

class BraveToolbarView : public ToolbarView,
                         public ProfileAttributesStorage::Observer,
                         public views::ViewTargeterDelegate {
  METADATA_HEADER(BraveToolbarView, ToolbarView)
 public:
  class LayoutGuard;

  explicit BraveToolbarView(Browser* browser, BrowserView* browser_view);
  ~BraveToolbarView() override;

  BraveBookmarkButton* bookmark_button() const { return bookmark_; }
  BraveShieldsToolbarButton* origin_shields_button() const {
    return origin_shields_button_;
  }
  WalletButton* wallet_button() const { return wallet_; }
  SidePanelButton* side_panel_button() const { return side_panel_; }
  ScreenshotButton* screenshot_button() const { return screenshot_button_; }
  ToolbarButton* vertical_tab_toggle_button() const {
    return vertical_tab_toggle_;
  }
  ToolbarButton* workspaces_button_for_testing() const {
    return workspaces_button_;
  }
  TabStripComboButton* combo_button() const { return combo_button_; }
#if BUILDFLAG(ENABLE_AI_CHAT)
  AIChatButton* ai_chat_button() const { return ai_chat_button_; }
#endif

#if BUILDFLAG(ENABLE_BRAVE_VPN)
  BraveVPNButton* brave_vpn_button() const { return brave_vpn_; }
  bool IsBraveVPNButtonVisible() const;
  void OnVPNButtonVisibilityChanged();
#endif

  void UpdateHorizontalPadding();
  void SetOriginPageChromeColors(SkColor surface,
                                 SkColor location_bar_color,
                                 SkColor location_bar_ring,
                                 SkColor foreground);

  std::optional<SkColor> origin_page_chrome_color_for_testing() const {
    return origin_page_chrome_surface_;
  }

  void Init() override;
  void Layout(PassKey) override;
  void OnPaintBackground(gfx::Canvas* canvas) override;
  void Update(content::WebContents* tab) override;
  void OnThemeChanged() override;
  void OnEditBookmarksEnabledChanged();
  void OnLocationBarIsWideChanged();
  void OnShowBookmarksButtonChanged();
  void OnShowScreenshotButtonChanged();
  void ShowBookmarkBubble(const GURL& url, bool already_bookmarked) override;
  void VisibilityChanged(views::View* starting_from, bool visible) override;
  void AddedToWidget() override;
  void RemovedFromWidget() override;

  // views::ViewTargeterDelegate:
  bool DoesIntersectRect(const views::View* target,
                         const gfx::Rect& rect) const override;

  // Origin's page chrome hides until it is wanted; call this to bring it back
  // for an action that needs the address field, such as Command+L.
  void RevealOriginPageChrome();

  // Reading moves the address field out of the way; coming back up brings it
  // back. Hover and omnibox focus still override both.
  void OnOriginPageScrolled(bool scrolled_down);

  bool origin_page_chrome_revealed() const {
    return origin_page_chrome_revealed_;
  }

 private:
  // views::View already declares OnEvent() with an incompatible signature, so
  // the pointer watch lives in its own observer.
  class OriginPointerWatcher;
  FRIEND_TEST_ALL_PREFIXES(BraveToolbarViewTest, ToolbarCornerRadiusTest);
  FRIEND_TEST_ALL_PREFIXES(BraveToolbarViewTest, ToolbarDividerNotShownTest);

  void LoadImages() override;
  void ResetLocationBarBounds();
  void ResetBookmarkButtonBounds();
  void EnsureOriginExtensionsToolbar();
  void UpdateOriginPageChromeControls();
  void SetOriginPageChromeRevealed(bool revealed);
  void OnOriginPointerMoved(const gfx::Point& screen_point);
  void ApplyOriginScrollState(bool scrolled_down);
  void ScheduleOriginPageChromeReveal(bool revealed);
  bool ShouldHoldOriginPageChromeOpen() const;
  // Width of the leading strip the window controls and navigation buttons
  // occupy, which stays live even while the rest of the bar is faded out.
  int GetOriginWindowControlsStripWidth() const;
  void UpdateBookmarkVisibility();
  void UpdateVerticalTabToggleVisibility();
  void UpdateVerticalTabTogglePlacement();
  void UpdateVerticalTabToggleState();
  void OnVerticalTabTogglePressed();
  void OnOriginQuickOpenPressed();
  void CreateWorkspaceButtonIfNeeded();
  void OnWorkspacesButtonPressed();
  void UpdateWorkspaceButtonVisibility();
  void UpdateWorkspaceButtonPlacement();
  void OnCompactModePrefChanged();
  void UpdateComboButtonState();
  bool IsFocusModeOverlayActive() const;

  // ProfileAttributesStorage::Observer:
  void OnProfileAdded(const base::FilePath& profile_path) override;
  void OnProfileWasRemoved(const base::FilePath& profile_path,
                           const std::u16string& profile_name) override;

#if BUILDFLAG(ENABLE_AI_CHAT)
  void UpdateAIChatButtonVisibility();
#endif
  void UpdateWalletButtonVisibility();

  ToolbarDivider* toolbar_divider_for_testing() { return toolbar_divider_; }

  raw_ptr<TabStripComboButton> combo_button_ = nullptr;

  raw_ptr<ToolbarButton> vertical_tab_toggle_ = nullptr;
  raw_ptr<ToolbarButton> origin_quick_open_button_ = nullptr;
  raw_ptr<ToolbarButton> workspaces_button_ = nullptr;
  raw_ptr<BraveBookmarkButton> bookmark_ = nullptr;
  raw_ptr<BraveShieldsToolbarButton> origin_shields_button_ = nullptr;
  // Tracks the preference to determine whether bookmark editing is allowed.
  BooleanPrefMember edit_bookmarks_enabled_;

  raw_ptr<WalletButton> wallet_ = nullptr;
  raw_ptr<SidePanelButton> side_panel_ = nullptr;

#if BUILDFLAG(ENABLE_BRAVE_VPN)
  raw_ptr<BraveVPNButton> brave_vpn_ = nullptr;
  BooleanPrefMember show_brave_vpn_button_;
  BooleanPrefMember hide_brave_vpn_button_by_policy_;
#endif

  BooleanPrefMember show_bookmarks_button_;

#if BUILDFLAG(ENABLE_AI_CHAT)
  raw_ptr<AIChatButton> ai_chat_button_ = nullptr;
  BooleanPrefMember show_ai_chat_button_;
  BooleanPrefMember hide_ai_chat_button_by_policy_;
#endif

  raw_ptr<ScreenshotButton> screenshot_button_ = nullptr;
  BooleanPrefMember show_screenshot_button_;

  BooleanPrefMember show_wallet_button_;
  BooleanPrefMember wallet_disabled_by_policy_;
  BooleanPrefMember wallet_private_window_enabled_;

  BooleanPrefMember location_bar_is_wide_;
  BooleanPrefMember compact_horizontal_tabs_;

  BooleanPrefMember show_vertical_tabs_;
  BooleanPrefMember show_title_bar_on_vertical_tabs_;
  BooleanPrefMember vertical_tabs_collapsed_;
  BooleanPrefMember vertical_tabs_on_right_;
  BooleanPrefMember show_vertical_tab_toggle_button_;
#if BUILDFLAG(IS_LINUX)
  BooleanPrefMember use_custom_chrome_frame_;
#endif  // BUILDFLAG(IS_LINUX)

  // Whether this toolbar has been initialized.
  bool brave_initialized_ = false;
  // Origin's top bar carries only the window controls until the pointer comes
  // to rest in it: the address field and page actions are transient.
  bool origin_page_chrome_revealed_ = true;
  bool origin_pointer_in_page_chrome_ = false;
  // Scrolling owns the bar: once the reader comes back up it stays until they
  // go down again. Starts true because a fresh page is at its top.
  bool origin_scroll_wants_page_chrome_ = true;
  base::OneShotTimer origin_page_chrome_reveal_timer_;
  base::OneShotTimer origin_scroll_settle_timer_;
  std::unique_ptr<OriginPointerWatcher> origin_page_chrome_pointer_watcher_;

  std::optional<SkColor> origin_page_chrome_surface_;
  std::optional<SkColor> origin_page_chrome_location_bar_;
  std::optional<SkColor> origin_page_chrome_location_bar_ring_;
  std::optional<SkColor> origin_page_chrome_foreground_;
  // Tracks profile count to determine whether profile switcher should be shown.
  base::ScopedObservation<ProfileAttributesStorage,
                          ProfileAttributesStorage::Observer>
      profile_observer_{this};
};

#endif  // BRAVE_BROWSER_UI_VIEWS_TOOLBAR_BRAVE_TOOLBAR_VIEW_H_
