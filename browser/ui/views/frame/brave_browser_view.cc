/* Copyright (c) 2019 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/views/frame/brave_browser_view.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/task/bind_post_task.h"
#include "base/task/sequenced_task_runner.h"
#include "brave/app/brave_command_ids.h"
#include "brave/browser/brave_browser_features.h"
#include "brave/browser/sparkle_buildflags.h"
#include "brave/browser/translate/brave_translate_utils.h"
#include "brave/browser/ui/brave_browser.h"
#include "brave/browser/ui/color/brave_color_id.h"
#include "brave/browser/ui/commands/accelerator_service.h"
#include "brave/browser/ui/commands/accelerator_service_factory.h"
#include "brave/browser/ui/focus_mode/focus_mode_features.h"
#include "brave/browser/ui/focus_mode/focus_mode_utils.h"
#include "brave/browser/ui/page_info/features.h"
#include "brave/browser/ui/sidebar/sidebar_controller.h"
#include "brave/browser/ui/sidebar/sidebar_utils.h"
#include "brave/browser/ui/sidebar/sidebar_web_panel_controller.h"
#include "brave/browser/ui/startup/origin_external_link_router.h"
#include "brave/browser/ui/tabs/brave_tab_prefs.h"
#include "brave/browser/ui/tabs/brave_tab_strip_model.h"
#include "brave/browser/ui/tabs/origin_media_monitor.h"
#include "brave/browser/ui/tabs/origin_space_controller.h"
#include "brave/browser/ui/tabs/public/vertical_tab_controller.h"
#include "brave/browser/ui/views/brave_actions/brave_actions_container.h"
#include "brave/browser/ui/views/brave_help_bubble/brave_help_bubble_host_view.h"
#include "brave/browser/ui/views/frame/focus_mode_title_bar_view.h"
#include "brave/browser/ui/views/frame/focus_mode_top_overlay.h"
#include "brave/browser/ui/views/frame/origin_quick_open_view.h"
#include "brave/browser/ui/views/frame/origin_site_identity.h"
#include "brave/browser/ui/views/frame/origin_temporary_link_view.h"
#include "brave/browser/ui/views/frame/origin_widgets/origin_widget_panel_view.h"
#include "brave/browser/ui/views/frame/split_view/brave_contents_container_view.h"
#include "brave/browser/ui/views/frame/split_view/brave_multi_contents_view.h"
#include "brave/browser/ui/views/frame/tab_strip_placement_coordinator.h"
#include "brave/browser/ui/views/frame/vertical_tabs/vertical_tab_strip_container_view.h"
#include "brave/browser/ui/views/frame/vertical_tabs/vertical_tab_strip_region_view.h"
#include "brave/browser/ui/views/location_bar/brave_location_bar_view.h"
#include "brave/browser/ui/views/omnibox/brave_omnibox_view_views.h"
#include "brave/browser/ui/views/side_panel/brave_side_panel_resize_area.h"
#include "brave/browser/ui/views/sidebar/sidebar_container_view.h"
#include "brave/browser/ui/views/toolbar/bookmark_button.h"
#include "brave/browser/ui/views/toolbar/brave_toolbar_view.h"
#include "brave/browser/ui/views/toolbar/screenshot_button.h"
#include "brave/browser/ui/views/window_closing_confirm_dialog_view.h"
#include "brave/common/pref_names.h"
#include "brave/components/brave_origin/buildflags/buildflags.h"
#include "brave/components/brave_wallet/common/buildflags/buildflags.h"
#include "brave/components/commands/common/features.h"
#include "brave/components/constants/pref_names.h"
#include "brave/components/sidebar/browser/constants.h"
#include "brave/components/sidebar/common/features.h"
#include "brave/components/speedreader/common/buildflags/buildflags.h"
#include "brave/components/vector_icons/vector_icons.h"
#include "brave/ui/color/nala/nala_color_id.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/app_mode/app_mode_utils.h"
#include "chrome/browser/autocomplete/autocomplete_classifier_factory.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/devtools/devtools_ui_controller.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "chrome/browser/favicon/favicon_utils.h"
#include "chrome/browser/ui/actions/chrome_action_id.h"
#include "chrome/browser/ui/bookmarks/bookmark_tab_helper.h"
#include "chrome/browser/ui/browser_actions.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/exclusive_access/exclusive_access_manager.h"
#include "chrome/browser/ui/frame/window_frame_util.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/side_panel/side_panel_entry.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "chrome/browser/ui/tabs/features.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/views/frame/browser_frame_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/browser_widget.h"
#include "chrome/browser/ui/views/frame/contents_web_view.h"
#include "chrome/browser/ui/views/frame/horizontal_tab_strip_region_view.h"
#include "chrome/browser/ui/views/frame/layout/browser_view_layout.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/frame/top_container_view.h"
#include "chrome/browser/ui/views/side_panel/side_panel.h"
#include "chrome/browser/ui/views/tab_search_bubble_host.h"
#include "chrome/browser/ui/views/tabs/shared/tab_strip_combo_button.h"
#include "chrome/browser/ui/views/tabs/shared/tab_strip_flat_edge_button.h"
#include "chrome/browser/ui/views/tabs/tab_strip.h"
#include "chrome/browser/ui/views/toolbar/browser_app_menu_button.h"
#include "chrome/browser/ui/web_applications/app_browser_controller.h"
#include "chrome/common/pref_names.h"
#include "components/javascript_dialogs/tab_modal_dialog_manager.h"
#include "components/omnibox/browser/autocomplete_classifier.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/omnibox/browser/location_bar_model.h"
#include "components/permissions/permission_request_manager.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/tab_interface.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "components/web_modal/web_contents_modal_dialog_manager.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "extensions/buildflags/buildflags.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/accelerators/accelerator_manager.h"
#include "ui/base/base_window.h"
#include "ui/base/hit_test.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/content_accelerators/accelerator_util.h"
#include "ui/display/screen.h"
#include "ui/events/event.h"
#include "ui/events/event_observer.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/color_analysis.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/image/image.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/native/native_view_host.h"
#include "ui/views/event_monitor.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_utils.h"

#if BUILDFLAG(ENABLE_EXTENSIONS)
#include "brave/browser/ui/views/brave_actions/brave_shields_action_view.h"
#include "brave/browser/ui/views/brave_actions/brave_shields_toolbar_button.h"
#endif

#if BUILDFLAG(ENABLE_BRAVE_WALLET)
#include "brave/browser/ui/views/toolbar/wallet_button.h"
#endif

#if BUILDFLAG(ENABLE_BRAVE_VPN)
#include "brave/browser/ui/views/toolbar/brave_vpn_button.h"
#include "brave/components/brave_vpn/common/pref_names.h"
#endif

#if BUILDFLAG(ENABLE_SPARKLE)
#include "brave/browser/ui/views/update_recommended_message_box_mac.h"
#endif

#if BUILDFLAG(ENABLE_SPEEDREADER)
#include "brave/browser/ui/speedreader/speedreader_tab_helper.h"
#include "brave/browser/ui/views/speedreader/reader_mode_bubble.h"
#include "brave/browser/ui/views/speedreader/reader_mode_toolbar_view.h"
#endif

#if BUILDFLAG(ENABLE_BRAVE_WAYBACK_MACHINE)
#include "brave/browser/ui/views/page_action/wayback_machine_bubble_view.h"
#endif

namespace {

// Exposed for testing.
constexpr float kBraveMinimumContrastRatioForOutlines = 1.0816f;

#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
struct OriginPageChromePalette {
  SkColor surface;
  SkColor location_bar;
  SkColor location_bar_ring;
  SkColor foreground;
};

bool IsUsefulOriginAccent(SkColor color) {
  if (SkColorGetA(color) == SK_AlphaTRANSPARENT) {
    return false;
  }

  SkScalar hsv[3];
  SkColorToHSV(color, hsv);
  return hsv[1] >= 0.12f;
}

std::optional<SkColor> GetOriginFaviconAccent(content::WebContents* contents) {
  if (!contents) {
    return std::nullopt;
  }

  const gfx::Image favicon = favicon::TabFaviconFromWebContents(contents);
  if (favicon.IsEmpty()) {
    return std::nullopt;
  }

  const SkBitmap bitmap = favicon.AsBitmap();
  if (bitmap.empty() || bitmap.isNull() || !bitmap.getPixels()) {
    return std::nullopt;
  }

  const SkColor accent = color_utils::CalculateKMeanColorOfBitmap(bitmap);
  return IsUsefulOriginAccent(accent) ? std::make_optional(accent)
                                      : std::nullopt;
}

// Origin has one continuous application shell. The page may choose the
// location-bar luminance, but it must never turn the toolbar into an extension
// of a light web page: Sigma keeps the frame, sidebar, and toolbar on the same
// dark surface and floats the page-coloured address field and web canvas above
// it.
OriginPageChromePalette ResolveOriginPageChromePalette(
    content::WebContents* contents,
    SkColor fallback,
    std::optional<SkColor> rendered_header_color) {
  std::optional<SkColor> page_color = rendered_header_color;
  if (contents) {
    if (!page_color) {
      page_color = contents->GetThemeColor();
    }
    if (!page_color || SkColorGetA(*page_color) == SK_AlphaTRANSPARENT) {
      const std::optional<SkColor> background = contents->GetBackgroundColor();
      // The rendered page background is the closest available representation
      // of the canvas itself (for example Hacker News' warm paper colour).
      // Preserve it even when it is low-saturation instead of replacing it
      // with a much louder favicon accent.
      page_color = background && SkColorGetA(*background) != SK_AlphaTRANSPARENT
                       ? background
                       : GetOriginFaviconAccent(contents);
      if (!page_color) {
        page_color = GetOriginKnownSiteAccent(contents->GetVisibleURL());
      }
    }
  }

  const SkColor candidate =
      page_color && SkColorGetA(*page_color) != SK_AlphaTRANSPARENT
          ? *page_color
          : fallback;
  const bool dark_shell = color_utils::IsDark(fallback);
  const SkColor location_base = dark_shell ? SkColorSetRGB(0x21, 0x23, 0x27)
                                           : SkColorSetRGB(0xF5, 0xF5, 0xF5);
  const SkColor opaque_candidate =
      color_utils::GetResultingPaintColor(candidate, location_base);

  return {
      .surface = fallback,
      .location_bar = color_utils::AlphaBlend(opaque_candidate, location_base,
                                              static_cast<SkAlpha>(0x4D)),
      .location_bar_ring = color_utils::AlphaBlend(
          opaque_candidate, location_base, static_cast<SkAlpha>(0x8C)),
      .foreground = SkColorSetRGB(0xE8, 0xE6, 0xE2),
  };
}

std::optional<SkColor> GetDominantOriginHeaderColor(const SkBitmap& bitmap) {
  if (bitmap.drawsNothing() || !bitmap.getPixels()) {
    return std::nullopt;
  }

  struct ColorBin {
    int count = 0;
    int red = 0;
    int green = 0;
    int blue = 0;
  };
  std::array<ColorBin, 16 * 16 * 16> bins;
  for (int y = 0; y < bitmap.height(); ++y) {
    for (int x = 0; x < bitmap.width(); ++x) {
      const SkColor color = bitmap.getColor(x, y);
      if (SkColorGetA(color) < 0x80) {
        continue;
      }
      SkScalar hsv[3];
      SkColorToHSV(color, hsv);
      // Ignore white page canvas, black type, and gray browser-like surfaces.
      // The remaining dominant bucket represents a rendered site masthead far
      // more reliably than favicon colour alone.
      if (hsv[1] < 0.16f || hsv[2] < 0.12f || hsv[2] > 0.98f) {
        continue;
      }
      const size_t bucket = (SkColorGetR(color) >> 4) << 8 |
                            (SkColorGetG(color) >> 4) << 4 |
                            (SkColorGetB(color) >> 4);
      ColorBin& bin = bins[bucket];
      ++bin.count;
      bin.red += SkColorGetR(color);
      bin.green += SkColorGetG(color);
      bin.blue += SkColorGetB(color);
    }
  }

  const ColorBin* dominant = nullptr;
  for (const ColorBin& bin : bins) {
    if (!dominant || bin.count > dominant->count) {
      dominant = &bin;
    }
  }
  if (!dominant || dominant->count < 8) {
    return std::nullopt;
  }
  return SkColorSetRGB(dominant->red / dominant->count,
                       dominant->green / dominant->count,
                       dominant->blue / dominant->count);
}
#endif

std::optional<bool> g_download_confirm_return_allow_for_testing;

bool IsUnsupportedCommand(int command_id, Browser* browser) {
  return IsRunningInForcedAppMode() &&
         !IsCommandAllowedInAppMode(
             command_id,
             browser->GetType() == BrowserWindowInterface::Type::TYPE_POPUP);
}

// A view that paints a background under the content area of the browser view so
// that the web content area can be displayed with rounded corners and a shadow.
class ContentsBackground : public views::View {
  METADATA_HEADER(ContentsBackground, views::View)
 public:
  ContentsBackground() {
    SetBackground(views::CreateSolidBackground(kColorToolbar));
    SetEnabled(false);

    // Prevent to eat any events that goes to web contents because web contents
    // could be behind this background.
    SetCanProcessEventsWithinSubtree(false);
  }
};
BEGIN_METADATA(ContentsBackground)
END_METADATA

bool ActivateOriginQuickOpenTab(Profile* profile, const GURL& url) {
  if (!profile || !url.is_valid()) {
    return false;
  }

  bool activated = false;
  ProfileBrowserCollection::GetForProfile(profile)->ForEach(
      [&](BrowserWindowInterface* browser_window) {
        TabStripModel* model = browser_window->GetTabStripModel();
        if (!model) {
          return true;
        }
        for (int index = 0; index < model->count(); ++index) {
          content::WebContents* contents = model->GetWebContentsAt(index);
          if (!contents || (contents->GetVisibleURL() != url &&
                            contents->GetLastCommittedURL() != url)) {
            continue;
          }
          model->ActivateTabAt(index);
          browser_window->GetWindow()->Show();
          browser_window->GetWindow()->Activate();
          activated = true;
          return false;
        }
        return true;
      },
      BrowserCollection::Order::kActivation);
  return activated;
}

}  // namespace

// static
void BraveBrowserView::SetDownloadConfirmReturnForTesting(bool allow) {
  g_download_confirm_return_allow_for_testing = allow;
}

class BraveBrowserView::BrowserWindowMouseEventHandler
    : public ui::EventObserver {
 public:
  explicit BrowserWindowMouseEventHandler(BraveBrowserView* browser_view)
      : browser_view_(browser_view) {
    auto* widget = browser_view_->GetWidget();
    CHECK(widget && widget->GetNativeWindow());

    // Use a window-scoped monitor so that mouse moves in other browser
    // windows don't get handled here. An application-wide monitor was used
    // previously to also get events when the browser widget is inactive
    // while an overlay widget is focused (ex, immersive mode on macOS), but
    // that observes mouse moves across *all* browser windows in the process,
    // causing hover-expand (vertical tabs/sidebar) to trigger in every open
    // window instead of just the one being hovered. See AddOverlayWidget()
    // for how the overlay-widget case is handled instead.
    monitor_ = views::EventMonitor::CreateWindowMonitor(
        this, widget->GetNativeWindow(), {ui::EventType::kMouseMoved});
  }

  ~BrowserWindowMouseEventHandler() override = default;

  BrowserWindowMouseEventHandler(const BrowserWindowMouseEventHandler&) =
      delete;
  BrowserWindowMouseEventHandler& operator=(
      const BrowserWindowMouseEventHandler&) = delete;

  // Adds a window-scoped monitor for an overlay widget belonging to this same
  // browser window (ex, immersive-fullscreen overlay widgets on macOS), so
  // that hover-expand keeps working while the overlay widget is focused
  // instead of the main browser widget.
  void AddOverlayWidget(views::Widget* overlay_widget) {
    if (!overlay_widget || !overlay_widget->GetNativeWindow()) {
      return;
    }
    overlay_monitors_.push_back(views::EventMonitor::CreateWindowMonitor(
        this, overlay_widget->GetNativeWindow(), {ui::EventType::kMouseMoved}));
  }

 private:
  // ui::EventObserver overrides:
  void OnEvent(const ui::Event& event) override {
    if (event.type() == ui::EventType::kMouseMoved) {
      browser_view_->HandleBrowserWindowMouseEvent(*event.AsMouseEvent());
      return;
    }
  }

  raw_ptr<BraveBrowserView> browser_view_ = nullptr;
  std::unique_ptr<views::EventMonitor> monitor_;
  std::vector<std::unique_ptr<views::EventMonitor>> overlay_monitors_;
};

class BraveBrowserView::TabCyclingEventHandler : public ui::EventObserver,
                                                 public views::WidgetObserver {
 public:
  explicit TabCyclingEventHandler(BraveBrowserView* browser_view)
      : browser_view_(browser_view) {
    Start();
  }

  ~TabCyclingEventHandler() override { Stop(); }

  TabCyclingEventHandler(const TabCyclingEventHandler&) = delete;
  TabCyclingEventHandler& operator=(const TabCyclingEventHandler&) = delete;

 private:
  // ui::EventObserver overrides:
  void OnEvent(const ui::Event& event) override {
    if (event.type() == ui::EventType::kKeyReleased &&
        event.AsKeyEvent()->key_code() == ui::VKEY_CONTROL) {
      // Ctrl key was released, stop the tab cycling
      Stop();
      return;
    }

    if (event.type() == ui::EventType::kMousePressed) {
      Stop();
    }
  }

  // views::WidgetObserver overrides:
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override {
    // We should stop cycling if other application gets active state.
    if (!active) {
      Stop();
    }
  }

  // Handle Browser widget closing while tab Cycling is in-progress.
  void OnWidgetClosing(views::Widget* widget) override { Stop(); }

  void Start() {
    // Add the event handler
    auto* widget = browser_view_->GetWidget();
    if (widget->GetNativeWindow()) {
      monitor_ = views::EventMonitor::CreateWindowMonitor(
          this, widget->GetNativeWindow(),
          {ui::EventType::kMousePressed, ui::EventType::kKeyReleased});
    }

    widget->AddObserver(this);
  }

  void Stop() {
    if (!monitor_.get()) {
      // We already stopped
      return;
    }

    // Remove event handler
    auto* widget = browser_view_->GetWidget();
    monitor_.reset();
    widget->RemoveObserver(this);
    browser_view_->StopTabCycling();
  }

  raw_ptr<BraveBrowserView> browser_view_ = nullptr;
  std::unique_ptr<views::EventMonitor> monitor_;
};

// static
BraveBrowserView* BraveBrowserView::From(BrowserView* view) {
  return views::AsViewClass<BraveBrowserView>(view);
}

// static
const BraveBrowserView* BraveBrowserView::From(const BrowserView* view) {
  return views::AsViewClass<const BraveBrowserView>(view);
}

// static
BraveBrowserView* BraveBrowserView::GetBrowserViewForBrowser(
    const BrowserWindowInterface* browser) {
  return From(BrowserView::GetBrowserViewForBrowser(browser));
}

bool BraveBrowserView::ShouldUseBraveWebViewRoundedCornersForContents(
    const BrowserWindowInterface* browser) {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  if (origin_external_link::IsTemporaryLinkBrowser(browser)) {
    return true;
  }
#endif
  if (browser->GetType() != BrowserWindowInterface::Type::TYPE_NORMAL) {
    return false;
  }

#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  // Origin treats web contents as an inset canvas within a unified chrome
  // shell. Keep this structural treatment independent of the Brave preference.
  return true;
#else
  if (browser->GetProfile()->GetPrefs()->GetBoolean(kWebViewRoundedCorners)) {
    return true;
  }

  auto* model = browser->GetTabStripModel();
  if (model->empty()) {
    return false;
  }

  if (TabStripModel::kNoTab == model->active_index()) {
    return false;
  }

  // Use rounded corners when browser view shows split view.
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  return browser_view && browser_view->multi_contents_view()->IsInSplitView();
#endif
}

BraveBrowserView::BraveBrowserView(Browser* browser) : BrowserView(browser) {
  CHECK(multi_contents_view_);

  // Upstream doesn't set icon because kFeatureTitleBar is not supported by
  // default via WindowFeatureController::SupportsWindowfeatures. In brave, we
  // support kFeatureTitleBar so it's set to true when browser is launched with
  // vertical tab mode. Set to false as we don't want to icon in title bar.
  if (browser_->GetType() == BrowserWindowInterface::Type::TYPE_NORMAL) {
    SetShowIcon(false);
  }

  tab_strip_placement_ = std::make_unique<TabStripPlacementCoordinator>(
      base::PassKey<BraveBrowserView>(), browser,
      horizontal_tab_strip_region_view_);

  // Need this background view always as we have contents margin/rounded corners
  // when split view is active regardless of rounded corners feature.
  contents_background_view_ =
      AddChildViewAt(std::make_unique<ContentsBackground>(), 0);

#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  if (origin_external_link::IsTemporaryLinkBrowser(browser_)) {
    origin_temporary_link_view_ =
        AddChildView(std::make_unique<OriginTemporaryLinkView>(browser_));
  } else {
    origin_empty_space_view_ = AddChildView(std::make_unique<views::View>());
    origin_empty_space_view_->SetBackground(
        views::CreateSolidBackground(kColorToolbar));
    origin_empty_space_view_->SetPaintToLayer();
    origin_empty_space_view_->layer()->SetFillsBoundsOpaquely(false);
    origin_empty_space_view_->layer()->SetRoundedCornerRadius(
        gfx::RoundedCornersF(10));
    origin_empty_space_view_->SetVisible(false);

    origin_quick_open_view_ =
        AddChildView(std::make_unique<OriginQuickOpenView>(
            browser_,
            base::BindRepeating(&BraveBrowserView::SubmitOriginQuickOpen,
                                base::Unretained(this)),
            base::BindRepeating(&BraveBrowserView::ShowOriginCommander,
                                base::Unretained(this)),
            base::BindRepeating(&BraveBrowserView::HideOriginQuickOpen,
                                base::Unretained(this))));

    origin_widget_panel_ = AddChildView(std::make_unique<OriginWidgetPanelView>(
        browser_, browser_->GetFeatures().origin_media_monitor()));
  }
#endif

  compact_horizontal_tabs_.Init(
      brave_tabs::kCompactHorizontalTabs, g_browser_process->local_state(),
      base::BindRepeating(&BraveBrowserView::OnCompactModePrefChanged,
                          base::Unretained(this)));

  pref_change_registrar_.Init(GetProfile()->GetPrefs());

  pref_change_registrar_.Add(
      kWebViewRoundedCorners,
      base::BindRepeating(&BraveBrowserView::OnPreferenceChanged,
                          base::Unretained(this)));

  pref_change_registrar_.Add(
      brave_tabs::kVerticalTabsEnabled,
      base::BindRepeating(&BraveBrowserView::OnPreferenceChanged,
                          base::Unretained(this)));

#if BUILDFLAG(ENABLE_BRAVE_VPN)
  pref_change_registrar_.Add(
      brave_vpn::prefs::kBraveVPNShowButton,
      base::BindRepeating(&BraveBrowserView::OnPreferenceChanged,
                          base::Unretained(this)));
#endif

  // Only normal window (tabbed) should have sidebar.
  const bool can_have_sidebar = sidebar::CanUseSidebar(browser_);
  if (can_have_sidebar) {
    sidebar_container_view_ =
        AddChildView(std::make_unique<SidebarContainerView>(browser_));
    // Recompute the panel's content corners whenever the sidebar control view
    // shows or hides, since the bottom corner radius depends on sidebar
    // control view visibility. Unretained() is safe: `this` owns
    // `sidebar_container_view_` (added via AddChildView), so the container
    // cannot outlive the callback target.
    sidebar_container_view_->SetSidebarControlViewVisibilityChangedCallback(
        base::BindRepeating(
            &BraveBrowserView::OnSidebarControlViewVisibilityChanged,
            base::Unretained(this)));

    side_panel_->SetResizeArea(
        std::make_unique<views::BraveSidePanelResizeArea>(side_panel_));

#if defined(USE_AURA)
    sidebar_host_view_ = AddChildView(std::make_unique<views::View>());
#endif

    pref_change_registrar_.Add(
        prefs::kSidePanelHorizontalAlignment,
        base::BindRepeating(&BraveBrowserView::OnPreferenceChanged,
                            base::Unretained(this)));
  }

  const bool supports_vertical_tabs =
      VerticalTabController::FromBrowser(browser_)->SupportsBraveVerticalTabs();
  if (supports_vertical_tabs) {
    vertical_tab_strip_host_view_ =
        AddChildView(std::make_unique<views::View>());
    vertical_tab_strip_host_view_->SetBackground(
        views::CreateSolidBackground(kColorToolbar));
  }

  if (BrowserSupportsFocusMode(browser_)) {
    auto* controller = browser_->GetFeatures().focus_mode_controller();
    CHECK(controller);
    focus_mode_observation_.Observe(controller);

    if (features::kFocusModeUrlDisplay.Get() ==
        features::FocusModeUrlDisplay::kTitleBar) {
      focus_mode_title_bar_view_ =
          AddChildView(std::make_unique<FocusModeTitleBarView>());
      focus_mode_title_bar_view_->SetVisible(false);
    }

    focus_mode_top_overlay_ =
        AddChildView(std::make_unique<FocusModeTopOverlay>(
            base::PassKey<BraveBrowserView>(), this));
  }

  EnsureFindBarHostViewIsLastChild();
}

void BraveBrowserView::Layout(PassKey) {
  LayoutSuperclass<BrowserView>(this);
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  if (origin_temporary_link_view_) {
    origin_temporary_link_view_->SetBounds(0, 0, width(),
                                           OriginTemporaryLinkView::kBarHeight);
    const gfx::Rect contents_bounds = contents_container()->bounds();
    contents_background_view_->SetBoundsRect(contents_bounds);
    if (auto* multi_contents = GetBraveMultiContentsView()) {
      multi_contents->SetBoundsRect(contents_container()->GetLocalBounds());
    }
    ReorderChildView(origin_temporary_link_view_, -1);
    EnsureFindBarHostViewIsLastChild();
    return;
  }
  if (origin_empty_space_view_) {
    auto* multi_contents = GetBraveMultiContentsView();
    gfx::Rect empty_space_bounds = multi_contents->GetMainContentsBounds();
    empty_space_bounds = views::View::ConvertRectToTarget(multi_contents, this,
                                                          empty_space_bounds);
    origin_empty_space_view_->SetBoundsRect(empty_space_bounds);
    if (origin_quick_open_view_) {
      origin_quick_open_view_->SetBoundsRect(empty_space_bounds);
    }
  }
#endif
}

void BraveBrowserView::SetOriginSpaceEmpty(bool empty) {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  if (!origin_empty_space_view_ ||
      origin_empty_space_view_->GetVisible() == empty) {
    return;
  }
  origin_empty_space_view_->SetVisible(empty);
  if (empty) {
    ReorderChildView(origin_empty_space_view_, -1);
    EnsureFindBarHostViewIsLastChild();
  }
  InvalidateLayout();
#endif
}

void BraveBrowserView::EnsureFindBarHostViewIsLastChild() {
  CHECK(find_bar_host_view_);

  // FindBarHost uses this view as kHostViewKey. See
  // BrowserView::find_bar_host_view().
  ReorderChildView(find_bar_host_view_, -1);
}

void BraveBrowserView::ShowOriginQuickOpen(
    std::optional<ui::KeyboardCode> activation_key) {
  if (!origin_quick_open_view_) {
    return;
  }
  origin_insert_mode_ = false;
  ReorderChildView(origin_quick_open_view_, -1);
  EnsureFindBarHostViewIsLastChild();
  origin_quick_open_view_->ShowAndFocus(activation_key);
  InvalidateLayout();
}

void BraveBrowserView::HideOriginQuickOpen() {
  if (!origin_quick_open_view_ || !origin_quick_open_view_->GetVisible()) {
    return;
  }
  origin_quick_open_view_->Dismiss();
  if (auto* contents = GetActiveWebContents()) {
    contents->Focus();
  }
}

void BraveBrowserView::ShowOriginCommander() {
  HideOriginQuickOpen();
  chrome::ExecuteCommand(browser(), IDC_COMMANDER);
}

void BraveBrowserView::SubmitOriginQuickOpen(
    OriginQuickOpenSelection selection,
    OriginQuickOpenDisposition disposition) {
  std::u16string input = std::move(selection.input);
  input = base::CollapseWhitespace(input, false);

  if (!selection.space_id.empty() && !selection.destination_url.is_valid()) {
    HideOriginQuickOpen();
    if (auto* controller = browser()->GetFeatures().origin_space_controller()) {
      controller->SelectSpace(selection.space_id);
    }
    return;
  }

  if (input.empty() && !selection.destination_url.is_valid()) {
    if (disposition == OriginQuickOpenDisposition::kReplace) {
      return;
    }
    HideOriginQuickOpen();
    if (disposition == OriginQuickOpenDisposition::kSplit) {
      chrome::NewSplitTab(browser(), split_tabs::SplitTabLayout::kSideBySide,
                          split_tabs::SplitTabCreatedSource::kKeyboardShortcut);
    } else {
      chrome::ExecuteCommand(browser(), IDC_NEW_TAB);
    }
    return;
  }

  if (disposition == OriginQuickOpenDisposition::kNewPage &&
      selection.switch_to_tab) {
    HideOriginQuickOpen();
    if (ActivateOriginQuickOpenTab(GetProfile(), selection.destination_url)) {
      return;
    }
  }

  GURL destination_url = std::move(selection.destination_url);
  if (!destination_url.is_valid()) {
    AutocompleteMatch match;
    AutocompleteClassifierFactory::GetForProfile(GetProfile())
        ->Classify(input, /*in_keyword_mode=*/false,
                   /*allow_exact_keyword_match=*/false,
                   metrics::OmniboxEventProto::INVALID_SPEC, &match, nullptr);
    destination_url = std::move(match.destination_url);
    if (!destination_url.is_valid()) {
      return;
    }
  }

  if (!selection.destination_space_id.empty()) {
    auto* controller = browser()->GetFeatures().origin_space_controller();
    if (!controller ||
        !controller->SelectSpace(selection.destination_space_id)) {
      return;
    }
  }

  HideOriginQuickOpen();
  switch (disposition) {
    case OriginQuickOpenDisposition::kNewPage: {
      // A fresh Space already owns a hidden Brave New Tab renderer so the
      // default page can be displayed immediately. Reuse it for the first
      // destination instead of leaving an invisible placeholder tab behind.
      auto* model = browser()->tab_strip_model();
      auto* controller = browser()->GetFeatures().origin_space_controller();
      if (controller &&
          controller->IsTabPlaceholder(model->GetActiveWebContents())) {
        NavigateParams params(browser(), destination_url,
                              ui::PAGE_TRANSITION_TYPED);
        params.disposition = WindowOpenDisposition::CURRENT_TAB;
        Navigate(&params);
        break;
      }
      browser()->tab_strip_model()->delegate()->AddTabAt(destination_url, -1,
                                                         true);
      break;
    }
    case OriginQuickOpenDisposition::kReplace: {
      NavigateParams params(browser(), destination_url,
                            ui::PAGE_TRANSITION_TYPED);
      params.disposition = WindowOpenDisposition::CURRENT_TAB;
      Navigate(&params);
      break;
    }
    case OriginQuickOpenDisposition::kSplit: {
      chrome::NewSplitTab(browser(), split_tabs::SplitTabLayout::kSideBySide,
                          split_tabs::SplitTabCreatedSource::kKeyboardShortcut);
      NavigateParams params(browser(), destination_url,
                            ui::PAGE_TRANSITION_TYPED);
      params.disposition = WindowOpenDisposition::CURRENT_TAB;
      Navigate(&params);
      break;
    }
  }
}

void BraveBrowserView::ToggleOriginWidgetPanel() {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  if (origin_widget_panel_) {
    origin_widget_panel_->TogglePanel();
  }
#endif
}

void BraveBrowserView::CloseActiveOriginTabTree() {
  auto* controller = browser()->GetFeatures().origin_space_controller();
  if (!controller || !controller->ActiveSpaceHasTabs()) {
    return;
  }

  auto* model =
      static_cast<BraveTabStripModel*>(browser()->tab_strip_model());
  const int active_index = model->active_index();
  if (active_index == TabStripModel::kNoTab) {
    return;
  }

  std::vector<int> indices =
      model->GetTreeTabDescendantIndices(active_index);
  indices.push_back(active_index);
  controller->SelectReplacementTabForClose(indices);
  model->CloseTabs(indices);
}

void BraveBrowserView::OnCompactModePrefChanged() {
  InvalidateLayout();
}

void BraveBrowserView::OnPreferenceChanged(const std::string& pref_name) {
  if (pref_name == brave_tabs::kVerticalTabsEnabled) {
    UpdateTabSearchBubbleHost();
    return;
  }

  if (pref_name == kWebViewRoundedCorners) {
    UpdateRoundedCornersUI();
    return;
  }

  if (pref_name == prefs::kSidePanelHorizontalAlignment) {
    UpdateSideBarHorizontalAlignment();
    return;
  }

#if BUILDFLAG(ENABLE_BRAVE_VPN)
  if (pref_name == brave_vpn::prefs::kBraveVPNShowButton) {
    vpn_panel_controller_.ResetBubbleManager();
    return;
  }
#endif
}

void BraveBrowserView::UpdateSideBarHorizontalAlignment() {
  DCHECK(sidebar_container_view_);

  const bool on_left = !GetProfile()->GetPrefs()->GetBoolean(
      prefs::kSidePanelHorizontalAlignment);

  // Panel has different border per horizontal alignment.
  side_panel_->UpdateBorder();

  sidebar_container_view_->SetSidebarOnLeft(on_left);

  if (base::FeatureList::IsEnabled(sidebar::features::kSidebarWebPanel)) {
    GetBraveMultiContentsView()->SetWebPanelOnLeft(on_left);
  }

  DeprecatedLayoutImmediately();
}

BraveBrowserView::~BraveBrowserView() {
  tab_cycling_event_handler_.reset();

  // Destroying delegate view to clear vertical tab state. See its dtor.
  if (vertical_tab_strip_container_view_) {
    auto container_view = RemoveChildViewT(vertical_tab_strip_container_view_);
    vertical_tab_strip_container_view_ = nullptr;
  }
}

sidebar::Sidebar* BraveBrowserView::InitSidebar() {
  // Start Sidebar UI initialization.
  DCHECK(sidebar_container_view_);
  sidebar_container_view_->Init();

  // Ask BraveMultiContentsView for preparing web panel feature.
  if (base::FeatureList::IsEnabled(sidebar::features::kSidebarWebPanel)) {
    GetBraveMultiContentsView()->SetWebPanelWidth(
        SidePanelEntry::kSidePanelDefaultContentWidth);
    GetBraveMultiContentsView()->UseContentsContainerViewForWebPanel();
  }

  UpdateSideBarHorizontalAlignment();

  return sidebar_container_view_;
}

void BraveBrowserView::ToggleSidebar() {
  browser_->GetFeatures().side_panel_ui()->Toggle();
}

void BraveBrowserView::ShowBraveVPNBubble(bool show_select) {
#if BUILDFLAG(ENABLE_BRAVE_VPN)
  vpn_panel_controller_.ShowBraveVPNPanel(show_select);
#endif
}

views::View* BraveBrowserView::GetAnchorViewForBraveVPNPanel() {
#if BUILDFLAG(ENABLE_BRAVE_VPN)
  auto* vpn_button =
      static_cast<BraveToolbarView*>(toolbar())->brave_vpn_button();
  if (vpn_button->GetVisible()) {
    return vpn_button;
  }
  return toolbar()->app_menu_button();
#else
  return nullptr;
#endif
}

gfx::Rect BraveBrowserView::GetShieldsBubbleRect() {
  if (web_app::AppBrowserController::IsWebApp(browser())) {
    if (page_info::features::IsShowBraveShieldsInPageInfoEnabled()) {
      // No PWA title-bar Shields anchor; the Page Info surface owns Shields, so
      // we do not add a second Shields toolbar button or bubble anchor here.
      return gfx::Rect();
    }

#if BUILDFLAG(ENABLE_EXTENSIONS)
    if (BraveShieldsToolbarButton* pwa = GetPwaShieldsToolbarButton()) {
      if (views::Widget* bubble = pwa->GetBubbleWidget()) {
        return bubble->GetClientAreaBoundsInScreen();
      }
    }
#endif

    // PWA window without a visible title-bar Shields control (e.g. bubble not
    // created yet, or install path did not add the button).
    return gfx::Rect();
  }

#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  auto* origin_toolbar = views::AsViewClass<BraveToolbarView>(toolbar());
  auto* origin_shields =
      origin_toolbar ? origin_toolbar->origin_shields_button() : nullptr;
  if (!origin_shields) {
    return gfx::Rect();
  }
  if (views::Widget* bubble = origin_shields->GetBubbleWidget()) {
    return bubble->GetClientAreaBoundsInScreen();
  }
  return gfx::Rect();
#else
  auto* brave_location_bar_view =
      static_cast<BraveLocationBarView*>(GetLocationBarView());
  if (!brave_location_bar_view) {
    return gfx::Rect();
  }

  auto* shields_action_view =
      brave_location_bar_view->brave_actions_contatiner_view()
          ->GetShieldsActionView();
  if (!shields_action_view) {
    return gfx::Rect();
  }

  auto* bubble_widget = shields_action_view->GetBubbleWidget();
  if (!bubble_widget) {
    return gfx::Rect();
  }

  return bubble_widget->GetClientAreaBoundsInScreen();
#endif
}

bool BraveBrowserView::GetTabStripVisible() const {
  if (auto* vtc = VerticalTabController::FromBrowser(browser());
      vtc->ShouldShowBraveVerticalTabs()) {
    return false;
  }

  return BrowserView::GetTabStripVisible();
}

void BraveBrowserView::SetStarredState(bool is_starred) {
  BraveBookmarkButton* button =
      static_cast<BraveToolbarView*>(toolbar())->bookmark_button();
  if (button) {
    button->SetToggled(is_starred);
  }
}

void BraveBrowserView::URLStarredChanged(content::WebContents* web_contents,
                                         bool starred) {
  if (web_contents == GetActiveWebContents()) {
    SetStarredState(starred);
  }
}

void BraveBrowserView::ObserveBookmarkTabHelper(
    content::WebContents* contents) {
  bookmark_tab_helper_observation_.Reset();
  if (auto* bookmark_helper =
          contents ? BookmarkTabHelper::FromWebContents(contents) : nullptr) {
    bookmark_tab_helper_observation_.Observe(bookmark_helper);
    SetStarredState(bookmark_helper->is_starred());
  } else {
    SetStarredState(false);
  }
}

void BraveBrowserView::OnActiveTabWillDiscardContents(
    tabs::TabInterface* tab,
    content::WebContents* old_contents,
    content::WebContents* new_contents) {
  ObserveBookmarkTabHelper(new_contents);
}

void BraveBrowserView::OnActiveTabWillDetach(
    tabs::TabInterface* tab,
    tabs::TabInterface::DetachReason reason) {
  bookmark_tab_helper_observation_.Reset();
}

#if BUILDFLAG(ENABLE_SPEEDREADER)
ReaderModeToolbarView* BraveBrowserView::reader_mode_toolbar() {
  return BraveContentsContainerView::From(
             GetBraveMultiContentsView()->GetActiveContentsContainerView())
      ->reader_mode_toolbar();
}

speedreader::SpeedreaderBubbleView* BraveBrowserView::ShowSpeedreaderBubble(
    speedreader::SpeedreaderTabHelper* tab_helper,
    speedreader::SpeedreaderBubbleLocation location) {
  views::View* anchor = nullptr;
  views::BubbleBorder::Arrow arrow = views::BubbleBorder::NONE;
  switch (location) {
    case speedreader::SpeedreaderBubbleLocation::kLocationBar:
      anchor = GetLocationBarView();
      arrow = views::BubbleBorder::TOP_RIGHT;
      break;
    case speedreader::SpeedreaderBubbleLocation::kToolbar:
      anchor = reader_mode_toolbar()->toolbar();
      arrow = views::BubbleBorder::TOP_LEFT;
      break;
  }

  auto* reader_mode_bubble =
      new speedreader::ReaderModeBubble(anchor, tab_helper);
  views::BubbleDialogDelegateView::CreateBubble(reader_mode_bubble);
  reader_mode_bubble->SetArrow(arrow);
  reader_mode_bubble->Show();
  return reader_mode_bubble;
}

void BraveBrowserView::UpdateReaderModeToolbar() {
  auto is_distilled = [](content::WebContents* web_contents) {
    if (!web_contents) {
      return false;
    }
    if (auto* th =
            speedreader::SpeedreaderTabHelper::FromWebContents(web_contents)) {
      return speedreader::IsDistilled(th->PageDistillState());
    }
    return false;
  };
  reader_mode_toolbar()->SetVisible(
      is_distilled(browser()->tab_strip_model()->GetActiveWebContents()));

  // Need to update inactive split tabs' reader mode toolbar because
  // it's also visible.
  auto* contents_container = BraveContentsContainerView::From(
      GetBraveMultiContentsView()->GetInactiveContentsContainerView());
  auto* reader_mode_toolbar = contents_container->reader_mode_toolbar();
  reader_mode_toolbar->SetVisible(
      is_distilled(contents_container->contents_view()->web_contents()));
}
#endif  // BUILDFLAG(ENABLE_SPEEDREADER)

void BraveBrowserView::ShowUpdateChromeDialog() {
#if BUILDFLAG(ENABLE_SPARKLE)
  // On mac, sparkle frameworks's relaunch api is used.
  UpdateRecommendedMessageBoxMac::Show(GetNativeWindow());
#else
  BrowserView::ShowUpdateChromeDialog();
#endif
}

gfx::Rect BraveBrowserView::GetBoundingBoxInScreenForMouseOverHandling() const {
  gfx::Rect browser_bounds = GetBoundsInScreen();
  gfx::Rect top_container_bounds = top_container_->GetBoundsInScreen();
  int top = top_container_bounds.bottom();
  return gfx::Rect(browser_bounds.x(), top, browser_bounds.width(),
                   browser_bounds.bottom() - top);
}

bool BraveBrowserView::HasSelectedURL() const {
  if (!GetLocationBarView() || !GetLocationBarView()->HasFocus()) {
    return false;
  }
  auto* brave_omnibox_view =
      static_cast<BraveOmniboxViewViews*>(GetLocationBarView()->omnibox_view());
  return brave_omnibox_view && brave_omnibox_view->SelectedTextIsURL();
}

void BraveBrowserView::CleanAndCopySelectedURL() {
  if (!GetLocationBarView()) {
    return;
  }
  auto* brave_omnibox_view =
      static_cast<BraveOmniboxViewViews*>(GetLocationBarView()->omnibox_view());
  if (!brave_omnibox_view) {
    return;
  }
  brave_omnibox_view->CleanAndCopySelectedURL();
}

#if BUILDFLAG(ENABLE_PLAYLIST_WEBUI)
void BraveBrowserView::ShowPlaylistBubble() {
  static_cast<BraveLocationBarView*>(GetLocationBarView())
      ->ShowPlaylistBubble();
}
#endif

#if BUILDFLAG(ENABLE_BRAVE_WAYBACK_MACHINE)
void BraveBrowserView::ShowWaybackMachineBubble() {
  views::View* const anchor =
      toolbar_button_provider()
          ->GetPageActionBubbleAnchor(kActionShowWaybackMachine)
          .GetIfView();
  if (!anchor) {
    return;
  }

  auto* item = actions::ActionManager::Get().FindAction(
      kActionShowWaybackMachine,
      BrowserActions::From(browser())->root_action_item());
  WaybackMachineBubbleView::Show(
      browser()->tab_strip_model()->GetActiveWebContents(), anchor, item);
}
#endif

#if BUILDFLAG(ENABLE_BRAVE_WALLET)
WalletButton* BraveBrowserView::GetWalletButton() {
  return static_cast<BraveToolbarView*>(toolbar())->wallet_button();
}

views::View* BraveBrowserView::GetWalletButtonAnchorView() {
  return static_cast<BraveToolbarView*>(toolbar())
      ->wallet_button()
      ->GetAsAnchorView();
}
#endif

void BraveBrowserView::OnAcceleratorsChanged(
    const commands::AcceleratorPrefManager::Accelerators& changed) {
  DCHECK(base::FeatureList::IsEnabled(commands::features::kBraveCommands));

  auto* focus_manager = GetFocusManager();
  DCHECK(focus_manager);

  for (const auto& [command_id, accelerators] : changed) {
    if (IsUnsupportedCommand(command_id, browser())) {
      continue;
    }

    std::vector<ui::Accelerator> old_accelerators;
    for (const auto& [accelerator, accelerator_command] : accelerator_table_) {
      if (accelerator_command != command_id) {
        continue;
      }
      old_accelerators.push_back(accelerator);
    }

    // Register current accelerators
    for (const auto& accelerator : accelerators) {
      if (focus_manager->IsAcceleratorRegistered(accelerator, this)) {
        focus_manager->UnregisterAccelerator(accelerator, this);
      }

      focus_manager->RegisterAccelerator(
          accelerator, ui::AcceleratorManager::kNormalPriority, this);
      accelerator_table_[accelerator] = command_id;
    }

    // Unregister removed accelerators
    for (const auto& old_accelerator : old_accelerators) {
      if (std::ranges::contains(accelerators, old_accelerator)) {
        continue;
      }
      focus_manager->UnregisterAccelerator(old_accelerator, this);
      accelerator_table_.erase(old_accelerator);
    }
  }
}

void BraveBrowserView::OnFocusModeToggled(bool enabled) {
  UpdateFocusModeState();
}

#if BUILDFLAG(ENABLE_BRAVE_WALLET)
void BraveBrowserView::CreateWalletBubble() {
  DCHECK(GetWalletButton());
  GetWalletButton()->ShowWalletBubble();
}

void BraveBrowserView::CreateApproveWalletBubble() {
  DCHECK(GetWalletButton());
  GetWalletButton()->ShowApproveWalletBubble();
}

void BraveBrowserView::CloseWalletBubble() {
  if (GetWalletButton()) {
    GetWalletButton()->CloseWalletBubble();
  }
}
#endif

void BraveBrowserView::AddedToWidget() {
  BrowserView::AddedToWidget();

  browser_window_mouse_event_handler_ =
      std::make_unique<BrowserWindowMouseEventHandler>(this);

  // we must call all new views once BraveBrowserView is added to widget

  GetBrowserViewLayout()->set_contents_background(contents_background_view_);
  GetBrowserViewLayout()->set_sidebar_container(sidebar_container_view_);
  GetBrowserViewLayout()->set_origin_widget_panel(origin_widget_panel_);

  if (vertical_tab_strip_host_view_) {
    vertical_tab_strip_container_view_ =
        AddChildView(std::make_unique<BraveVerticalTabStripContainerView>(
            this, vertical_tab_strip_host_view_));
    GetBrowserViewLayout()->set_vertical_tab_strip_host(
        vertical_tab_strip_host_view_.get());
  }

  if (focus_mode_title_bar_view_) {
    GetBrowserViewLayout()->set_focus_mode_title_bar(
        focus_mode_title_bar_view_);
  }

  UpdateFocusModeState();
  EnsureFindBarHostViewIsLastChild();
}

void BraveBrowserView::RemovedFromWidget() {
  focus_mode_observation_.Reset();
  BrowserView::RemovedFromWidget();
}

#if BUILDFLAG(IS_MAC)
views::View* BraveBrowserView::CreateMacOverlayView() {
  auto* overlay_view = BrowserView::CreateMacOverlayView();

  // Hover-expand (vertical tabs/sidebar) needs mouse events while the overlay
  // widget is focused instead of this window's main widget (ex, immersive
  // fullscreen). `browser_window_mouse_event_handler_` normally only monitors
  // the main widget, so add the overlay widgets it created above too.
  CHECK(browser_window_mouse_event_handler_);
  browser_window_mouse_event_handler_->AddOverlayWidget(overlay_widget());
  browser_window_mouse_event_handler_->AddOverlayWidget(tab_overlay_widget());

  return overlay_view;
}
#endif

bool BraveBrowserView::ShowBraveHelpBubbleView(const std::string& text) {
#if !BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  if (page_info::features::IsShowBraveShieldsInPageInfoEnabled()) {
    // Shields in Page Info: no anchored toolbar / title-bar help target here.
    return false;
  }
#endif

  views::View* shield_icon = nullptr;
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  if (web_app::AppBrowserController::IsWebApp(browser())) {
    if (page_info::features::IsShowBraveShieldsInPageInfoEnabled()) {
      return false;
    }
#if BUILDFLAG(ENABLE_EXTENSIONS)
    shield_icon = GetPwaShieldsToolbarButton();
#endif
  } else if (auto* origin_toolbar =
                 views::AsViewClass<BraveToolbarView>(toolbar())) {
    shield_icon = origin_toolbar->origin_shields_button();
  }
#else
  if (web_app::AppBrowserController::IsWebApp(browser())) {
#if BUILDFLAG(ENABLE_EXTENSIONS)
    shield_icon = GetPwaShieldsToolbarButton();
#endif  // BUILDFLAG(ENABLE_EXTENSIONS)
  } else {
    auto* brave_location_bar_view =
        static_cast<BraveLocationBarView*>(GetLocationBarView());
    if (!brave_location_bar_view) {
      return false;
    }
    auto* c = brave_location_bar_view->brave_actions_contatiner_view();
    if (!c) {
      return false;
    }
    shield_icon = c->GetShieldsActionView();
  }
#endif
  if (!shield_icon || !shield_icon->GetVisible()) {
    return false;
  }

  // When help bubble is closed, this host view gets hidden.
  // For now, this help bubble host view is only used for shield icon, but it
  // could be re-used for other icons or views in the future.
  if (!brave_help_bubble_host_view_) {
    brave_help_bubble_host_view_ =
        AddChildView(std::make_unique<BraveHelpBubbleHostView>());
  }
  brave_help_bubble_host_view_->set_text(text);
  brave_help_bubble_host_view_->set_tracked_element(shield_icon);
  return brave_help_bubble_host_view_->Show();
}

void BraveBrowserView::LoadAccelerators() {
  if (base::FeatureList::IsEnabled(commands::features::kBraveCommands)) {
    auto* accelerator_service =
        commands::AcceleratorServiceFactory::GetForContext(
            browser()->GetProfile());
    if (accelerator_service) {
      accelerators_observation_.Observe(accelerator_service);
      return;
    }
  }
  BrowserView::LoadAccelerators();
}

void BraveBrowserView::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  BrowserView::OnTabStripModelChanged(tab_strip_model, change, selection);

  if (change.type() != TabStripModelChange::kSelectionOnly) {
    // Stop tab cycling if tab is closed dusing the cycle.
    // This can happen when tab is closed by shortcut (ex, ctrl + F4).
    // After stopping, current tab cycling, new tab cycling will be started.
    StopTabCycling();
  }

  if (selection.active_tab_changed() && brave_help_bubble_host_view_ &&
      brave_help_bubble_host_view_->GetVisible()) {
    brave_help_bubble_host_view_->Hide();
  }

  if (selection.active_tab_changed()) {
    if (focus_mode_title_bar_view_ &&
        focus_mode_title_bar_view_->GetVisible()) {
      focus_mode_title_bar_view_->SetTab(
          browser()->tab_strip_model()->GetActiveTab());
    }
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
    if (origin_temporary_link_view_) {
      origin_temporary_link_view_->Update();
    }
#endif
  }
}

views::CloseRequestResult BraveBrowserView::OnWindowCloseRequested() {
  if (GetBraveBrowser()->ShouldAskForBrowserClosingBeforeHandlers()) {
    if (!closing_confirm_dialog_activated_) {
      WindowClosingConfirmDialogView::Show(
          browser(),
          base::BindOnce(&BraveBrowserView::OnWindowClosingConfirmResponse,
                         weak_ptr_.GetWeakPtr()));
      closing_confirm_dialog_activated_ = true;
    }
    return views::CloseRequestResult::kCannotClose;
  }

  return BrowserView::OnWindowCloseRequested();
}

void BraveBrowserView::OnWindowClosingConfirmResponse(bool allowed_to_close) {
  DCHECK(closing_confirm_dialog_activated_);
  closing_confirm_dialog_activated_ = false;

  auto* browser = GetBraveBrowser();
  // Record the user's choice on the window-scoped UnloadController, which
  // tracks the result of any warning or beforeunload handlers.
  UnloadController::From(browser)->set_confirmed_to_close(allowed_to_close);
  if (allowed_to_close) {
    // Start close window again as user allowed to close it.
    // Confirm dialog will not be launched for this closing request
    // as we set UnloadController::confirmed_to_close_ to true.
    // If user cancels this window closing via additional warnings
    // or beforeunload handler, this dialog will be shown again.
    chrome::CloseWindow(browser);
  }
}

void BraveBrowserView::ConfirmBrowserCloseWithPendingDownloads(
    int download_count,
    UnloadController::DownloadCloseType dialog_type,
    base::OnceCallback<void(bool)> callback) {
  // Simulate user response.
  if (g_download_confirm_return_allow_for_testing) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback),
                       *g_download_confirm_return_allow_for_testing));
    return;
  }
  BrowserView::ConfirmBrowserCloseWithPendingDownloads(
      download_count, dialog_type, std::move(callback));
}

bool BraveBrowserView::MaybeUpdateDevtools(content::WebContents* web_contents) {
  CHECK(!web_contents || web_contents == GetActiveWebContents())
      << "This method is supposed to be called only for the active web "
         "contents";

  // In BrowserView::MaybeUpdateDevtools(), there is assumption that
  // split tab is active when MultiContentsView shows split view now.
  // But, it could not when web panel is active and split view is opened
  // together. Early return to avoid crash from that.
  if (IsWebPanelContents(web_contents) && IsInSplitView()) {
    return browser_->GetFeatures().devtools_ui_controller()->UpdateDevtools(
        web_contents, false);
  }

  bool result = BrowserView::MaybeUpdateDevtools(web_contents);

  // The devtools web view shares the contents area's corner radii, which are
  // applied at the end of a layout pass.
  InvalidateLayout();
  return result;
}

bool BraveBrowserView::MaybeUpdateSplitView(
    content::WebContents* web_contents) {
  // Don't need to update split view state if |web_contents| is web panel.
  // In BrowserView::MaybeUpdateSplitView(), there is assumption that
  // split tab is active when MultiContentsView shows split view now.
  // But, it could not when web panel is active and split view is opened
  // together. Early return to avoid crash from that. If |web_contents| is not
  // related with split tab, don't need to call base class' method.
  if (IsWebPanelContents(web_contents) &&
      multi_contents_view_->IsInSplitView()) {
    return false;
  }

  return BrowserView::MaybeUpdateSplitView(web_contents);
}

void BraveBrowserView::OnWidgetActivationChanged(views::Widget* widget,
                                                 bool active) {
  BrowserView::OnWidgetActivationChanged(widget, active);

  // For updating sidebar's item state.
  // As we can activate other window's Talk tab with current window's sidebar
  // Talk item, sidebar Talk item should have activated state if other windows
  // have Talk tab. It would be complex to get updated when Talk tab is opened
  // from other windows. So, simply trying to update when window activation
  // state is changed. With this, active window could have correct sidebar
  // item state.
  if (sidebar_container_view_) {
    sidebar_container_view_->UpdateSidebarItemsState();
  }
}

void BraveBrowserView::OnWidgetWindowModalVisibilityChanged(
    views::Widget* widget,
    bool visible) {
  // We explicitly override this and don't call the parent class, because we
  // currently don't support scrim views for tab modals and thus don't want the
  // parent class to make the scrim view visible
}

void BraveBrowserView::ShowSplitView(bool focus_active_view) {
  BrowserView::ShowSplitView(focus_active_view);

  UpdateRoundedCornersUI();
}

void BraveBrowserView::HideSplitView() {
  BrowserView::HideSplitView();

  UpdateRoundedCornersUI();
}

void BraveBrowserView::ReparentTopContainerForEndOfImmersive() {
  if (VerticalTabController::FromBrowser(browser())
          ->ShouldShowBraveVerticalTabs() ||
      IsFocusModeEnabled(browser())) {
    return;
  }

  BrowserView::ReparentTopContainerForEndOfImmersive();
}

bool BraveBrowserView::ShouldDrawTabStrokes() const {
  // TODO(simonhong): We can return false always here as horizontal tab design
  // doesn't need additional stroke.
  // Delete all below code when horizontal tab feature flag is removed.
  if (tabs::HorizontalTabsUpdateEnabled()) {
    // We never automatically draw strokes around tabs. For pinned tabs, we draw
    // the stroke when generating the tab drawing path.
    return false;
  }

  if (!BrowserView::ShouldDrawTabStrokes()) {
    return false;
  }

  // Use a little bit lower minimum contrast ratio as our ratio is 1.08162
  // between default tab background and frame color of light theme.
  // With upstream's 1.3f minimum ratio, strokes are drawn and it causes weird
  // border lines in the tab group.
  // Set 1.0816f as a minimum ratio to prevent drawing stroke.
  // We don't need the stroke for our default light theme.
  // NOTE: We don't need to check features::kTabOutlinesInLowContrastThemes
  // enabled state. Although TabStrip::ShouldDrawTabStrokes() has related code,
  // that feature is already expired since cr82. See
  // chrome/browser/flag-metadata.json.
  const SkColor background_color = TabStyle::Get()->GetTabBackgroundColor(
      TabStyle::TabSelectionState::kActive, /*hovered=*/false,
      /*frame_active*/ true, GetColorProvider());
  const SkColor frame_color =
      GetFrameView()->GetFrameColor(BrowserFrameActiveState::kActive);
  const float contrast_ratio =
      color_utils::GetContrastRatio(background_color, frame_color);
  return contrast_ratio < kBraveMinimumContrastRatioForOutlines;
}

void BraveBrowserView::UpdateTabSearchBubbleHost() {
  if (!GetIsNormalType()) {
    return;
  }

#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  // Origin replaces the vertical-tabs combo button with its own Quick Open
  // button. There is therefore no TabStripComboButton to host Chromium's tab
  // search bubble while vertical tabs are active. Quick Open owns search in
  // this mode, both for the toolbar button and the single-key shortcut.
  if (VerticalTabController::FromBrowser(browser())
          ->ShouldShowBraveVerticalTabs()) {
    tab_search_bubble_host_.reset();
    return;
  }
#endif

  BrowserView::UpdateTabSearchBubbleHost();

  auto* tab_search_action = actions::ActionManager::Get().FindAction(
      kActionTabSearch, BrowserActions::From(browser_)->root_action_item());
  CHECK(tab_search_action);

  // As we use toolbar's combo button in vertical tab mode, host should be
  // re-initialzed with it.
  if (VerticalTabController::FromBrowser(browser())
          ->ShouldShowBraveVerticalTabs()) {
    auto* toolbar_view = views::AsViewClass<BraveToolbarView>(toolbar());
    auto* combo_button = toolbar_view->combo_button();
    tab_search_bubble_host_ = std::make_unique<TabSearchBubbleHost>(
        combo_button->end_button(), browser_.get());
    tab_search_bubble_host_->set_use_brave_vertical_tab();
    combo_button->SetTabSearchBubbleHost(tab_search_bubble_host_.get());

    tab_search_action->SetImage(
        ui::ImageModel::FromVectorIcon(kLeoWindowSearchIcon));
  } else {
    // In horizontal tab mode, we can use upstream's host as it refers tab
    // strip's combo button. We use that combo button. Just re-arranged its
    // position.
    tab_search_action->SetImage(
        ui::ImageModel::FromVectorIcon(kLeoCaratDownIcon));
  }
}

BraveMultiContentsView* BraveBrowserView::GetBraveMultiContentsView() const {
  return BraveMultiContentsView::From(multi_contents_view_);
}

bool BraveBrowserView::ShouldShowWindowTitle() const {
  if (BrowserView::ShouldShowWindowTitle()) {
    return true;
  }

  if (VerticalTabController::FromBrowser(browser())
          ->ShouldShowWindowTitleForVerticalTabs()) {
    return true;
  }

  return false;
}

void BraveBrowserView::UpdateRoundedCornersUI() {
  // Update various UI that can be affected by rounded corners. The contents
  // corner radii themselves are applied by the layout.
  UpdateVerticalTabStripBorder();
  UpdateSidebarBorder();
  InvalidateLayout();
}

void BraveBrowserView::UpdateVerticalTabStripBorder() {
  // Vertical tab strip's border could be toggled based on split view state.
  if (vertical_tab_strip_container_view_) {
    vertical_tab_strip_container_view_->vertical_tab_strip_region_view()
        ->UpdateBorder();
  }
}

void BraveBrowserView::FinalizeOriginContentsResize() {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  // NativeViewHost keeps the renderer in a separate native child view on
  // macOS. During a sequence of animated bounds changes, its final clip and
  // surface can otherwise remain at an intermediate size, leaving a large
  // page-coloured region where newly exposed renderer tiles should be. This is
  // the same final-layout step BrowserView performs after toolbar animation
  // and tab dragging.
  multi_contents_view_->SetIsAnimatingContent(false);
  multi_contents_view_->ExecuteOnEachVisibleContentsView(
      base::BindRepeating([](ContentsWebView* contents_view) {
        contents_view->InvalidateLayout();
      }));

  InvalidateLayout();
  DeprecatedLayoutImmediately();
  contents_container()->DeprecatedLayoutImmediately();

  // Make the destination viewport explicit to the renderer after the native
  // holder has reached its final bounds. This also guarantees a fresh local
  // surface when the sidebar crosses a responsive breakpoint.
  multi_contents_view_->ExecuteOnEachVisibleContentsView(
      base::BindRepeating([](ContentsWebView* contents_view) {
        content::WebContents* web_contents = contents_view->web_contents();
        if (!web_contents) {
          return;
        }
        content::RenderWidgetHostView* render_view =
            web_contents->GetRenderWidgetHostView();
        if (render_view && render_view->GetRenderWidgetHost()) {
          render_view->GetRenderWidgetHost()->SynchronizeVisualProperties();
        }
      }));
#endif
}

void BraveBrowserView::UpdateSidebarBorder() {
  if (side_panel_) {
    side_panel_->SetRoundedBorderEnabled(
        ShouldUseBraveWebViewRoundedCornersForContents(browser_));
  }

  if (sidebar_container_view_) {
    sidebar_container_view_->UpdateBorder();
  }
}

void BraveBrowserView::OnSidebarControlViewVisibilityChanged() {
  // The panel's content corner radii depend on whether the sidebar control view
  // is visible (see brave::GetPanelContentsRoundedCorners()), so re-apply the
  // border to recompute them.
  if (side_panel_) {
    side_panel_->UpdateBorder();
  }
}

BraveShieldsToolbarButton* BraveBrowserView::GetPwaShieldsToolbarButton() {
  return pwa_shields_toolbar_button_.get();
}

void BraveBrowserView::SetPwaShieldsToolbarButton(
    BraveShieldsToolbarButton* button) {
  CHECK(!pwa_shields_toolbar_button_)
      << "PWA Shields toolbar button should only be set once";
  pwa_shields_toolbar_button_ = button;
}

void BraveBrowserView::OnActiveTabChanged(content::WebContents* old_contents,
                                          content::WebContents* new_contents,
                                          int index,
                                          int reason) {
  bool tab_change_in_split_view =
      IsTabChangeInSplitView(old_contents, new_contents);

  BrowserView::OnActiveTabChanged(old_contents, new_contents, index, reason);

#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  UpdateOriginPageChromeColor(new_contents);
  if (origin_temporary_link_view_) {
    origin_temporary_link_view_->Update();
  }
#endif

  ObserveBookmarkTabHelper(new_contents);
  active_tab_will_discard_contents_subscription_ = {};
  active_tab_will_detach_subscription_ = {};
  if (auto* tab = new_contents
                      ? tabs::TabInterface::GetFromContents(new_contents)
                      : nullptr) {
    active_tab_will_discard_contents_subscription_ =
        tab->RegisterWillDiscardContents(base::BindRepeating(
            &BraveBrowserView::OnActiveTabWillDiscardContents,
            base::Unretained(this)));
    active_tab_will_detach_subscription_ =
        tab->RegisterWillDetach(base::BindRepeating(
            &BraveBrowserView::OnActiveTabWillDetach, base::Unretained(this)));
  }

  // Switching between tabs may change state that is relevant for focus mode
  // (e.g. when switching between an https tab and an http tab).
  UpdateFocusModeState();

  // In focus mode, when switching between tabs that aren't split-view pairs
  // temporarily reveal the location bar.
  if (focus_mode_top_overlay_ && !tab_change_in_split_view) {
    focus_mode_top_overlay_->RevealTemporarily(base::Seconds(2));
  }

  // Update UI after active tab changing is handled because
  // ShouldUseBraveWebViewRoundedCornersForContents() check split view UI for
  // rounded corners.
  UpdateRoundedCornersUI();

#if BUILDFLAG(ENABLE_SPEEDREADER)
  UpdateReaderModeToolbar();
#endif

  // Some managers need to consider tab's active state with web content's
  // visibility.
  if (old_contents) {
    auto* permission_manager =
        permissions::PermissionRequestManager::FromWebContents(old_contents);
    CHECK(permission_manager);
    permission_manager->OnTabActiveStateChanged(false);

    // web/tab modal dialog manger can get tab activation state fromm their
    // delegates.
    auto* web_modal_dialog_manager =
        web_modal::WebContentsModalDialogManager::FromWebContents(old_contents);
    CHECK(web_modal_dialog_manager);
    web_modal_dialog_manager->OnTabActiveStateChanged();

    auto* tab_modal_dialog_manager =
        javascript_dialogs::TabModalDialogManager::FromWebContents(
            old_contents);
    CHECK(tab_modal_dialog_manager);
    tab_modal_dialog_manager->OnTabActiveStateChanged();
  }

  if (new_contents) {
    auto* permission_manager =
        permissions::PermissionRequestManager::FromWebContents(new_contents);
    CHECK(permission_manager);
    permission_manager->OnTabActiveStateChanged(true);

    auto* web_modal_dialog_manager =
        web_modal::WebContentsModalDialogManager::FromWebContents(new_contents);
    CHECK(web_modal_dialog_manager);
    web_modal_dialog_manager->OnTabActiveStateChanged();

    auto* tab_modal_dialog_manager =
        javascript_dialogs::TabModalDialogManager::FromWebContents(
            new_contents);
    CHECK(tab_modal_dialog_manager);
    tab_modal_dialog_manager->OnTabActiveStateChanged();
  }
}

void BraveBrowserView::UpdateToolbar(content::WebContents* contents) {
  BrowserView::UpdateToolbar(contents);

#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  UpdateOriginPageChromeColor(contents ? contents : GetActiveWebContents());
  if (origin_temporary_link_view_) {
    origin_temporary_link_view_->Update();
  }
#endif

  // Re-evaluate focus mode on every active-tab toolbar refresh. Same-tab
  // navigations that only change the security level between non-secure states
  // (e.g. an https page to a file:// page) do not produce a security-state
  // change, so `UpdateToolbarSecurityState` alone is not sufficient.
  UpdateFocusModeState();
}

bool BraveBrowserView::UpdateToolbarSecurityState() {
  bool state_changed = BrowserView::UpdateToolbarSecurityState();
  if (state_changed) {
    UpdateFocusModeState();
  }
  return state_changed;
}

void BraveBrowserView::OnThemeChanged() {
  BrowserView::OnThemeChanged();

#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  UpdateOriginPageChromeColor(GetActiveWebContents());
#endif
}

void BraveBrowserView::UpdateOriginPageChromeColor(
    content::WebContents* contents) {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  const ui::ColorProvider* colors = GetColorProvider();
  if (!colors) {
    return;
  }

  ScheduleOriginPageHeaderColorSample(contents);
  const GURL visible_url = contents ? contents->GetVisibleURL() : GURL();
  const std::optional<SkColor> rendered_header_color =
      visible_url == origin_page_header_sampled_url_ ? origin_page_header_color_
                                                     : std::nullopt;
  const OriginPageChromePalette palette = ResolveOriginPageChromePalette(
      contents, colors->GetColor(kColorToolbar), rendered_header_color);
  if (origin_page_chrome_surface_ == palette.surface &&
      origin_page_chrome_location_bar_ == palette.location_bar &&
      origin_page_chrome_location_bar_ring_ == palette.location_bar_ring &&
      origin_page_chrome_foreground_ == palette.foreground) {
    return;
  }
  origin_page_chrome_surface_ = palette.surface;
  origin_page_chrome_location_bar_ = palette.location_bar;
  origin_page_chrome_location_bar_ring_ = palette.location_bar_ring;
  origin_page_chrome_foreground_ = palette.foreground;

  if (contents_background_view_) {
    contents_background_view_->SetBackground(
        views::CreateSolidBackground(palette.surface));
  }
  if (origin_empty_space_view_) {
    origin_empty_space_view_->SetBackground(
        views::CreateSolidBackground(palette.surface));
  }
  if (auto* toolbar_view = views::AsViewClass<BraveToolbarView>(toolbar())) {
    toolbar_view->SetOriginPageChromeColors(
        palette.surface, palette.location_bar, palette.location_bar_ring,
        palette.foreground);
  }
#endif
}

void BraveBrowserView::ScheduleOriginPageHeaderColorSample(
    content::WebContents* contents) {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  if (!contents || contents != GetActiveWebContents()) {
    return;
  }
  const GURL url = contents->GetVisibleURL();
  if (!url.SchemeIsHTTPOrHTTPS() || url == origin_page_header_sampled_url_ ||
      url == origin_page_header_sample_pending_url_) {
    return;
  }

  origin_page_header_sample_timer_.Stop();
  origin_page_header_sample_pending_url_ = url;
  origin_page_header_sample_timer_.Start(
      FROM_HERE, base::Milliseconds(350),
      base::BindOnce(&BraveBrowserView::SampleOriginPageHeaderColor,
                     weak_ptr_.GetWeakPtr(), contents, url));
#endif
}

void BraveBrowserView::SampleOriginPageHeaderColor(
    content::WebContents* contents,
    const GURL& url) {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  if (!contents || contents != GetActiveWebContents() ||
      contents->GetVisibleURL() != url) {
    if (origin_page_header_sample_pending_url_ == url) {
      origin_page_header_sample_pending_url_ = GURL();
    }
    return;
  }
  content::RenderWidgetHostView* render_view =
      contents->GetRenderWidgetHostView();
  if (!render_view) {
    origin_page_header_sample_pending_url_ = GURL();
    return;
  }

  const gfx::Size viewport = render_view->GetVisibleViewportSize();
  if (viewport.IsEmpty()) {
    origin_page_header_sample_pending_url_ = GURL();
    return;
  }
  constexpr int kMaximumSourceWidth = 720;
  constexpr int kSourceHeight = 72;
  constexpr int kOutputWidth = 240;
  constexpr int kOutputHeight = 32;
  const int source_width = std::min(viewport.width(), kMaximumSourceWidth);
  const int source_height = std::min(viewport.height(), kSourceHeight);
  const gfx::Rect source_rect((viewport.width() - source_width) / 2, 0,
                              source_width, source_height);
  const gfx::Size output_size(std::min(source_width, kOutputWidth),
                              std::min(source_height, kOutputHeight));

  auto on_copied = base::BindPostTaskToCurrentDefault(base::BindOnce(
      [](base::WeakPtr<BraveBrowserView> browser_view,
         content::WebContents* sampled_contents, GURL sampled_url,
         const content::CopyFromSurfaceResult& result) {
        if (!browser_view ||
            browser_view->origin_page_header_sample_pending_url_ !=
                sampled_url) {
          return;
        }
        browser_view->origin_page_header_sample_pending_url_ = GURL();
        if (sampled_contents != browser_view->GetActiveWebContents() ||
            sampled_contents->GetVisibleURL() != sampled_url) {
          return;
        }
        browser_view->origin_page_header_sampled_url_ = sampled_url;
        browser_view->origin_page_header_color_ =
            result.has_value() ? GetDominantOriginHeaderColor(result->bitmap)
                               : std::nullopt;
        browser_view->UpdateOriginPageChromeColor(sampled_contents);
      },
      weak_ptr_.GetWeakPtr(), contents, url));
  render_view->CopyFromSurface(source_rect, output_size, base::Seconds(2),
                               std::move(on_copied));
#endif
}

void BraveBrowserView::DidChangeThemeColor() {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  UpdateOriginPageChromeColor(web_contents());
#endif
}

void BraveBrowserView::OnBackgroundColorChanged() {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  UpdateOriginPageChromeColor(web_contents());
#endif
}

void BraveBrowserView::DidStopLoading() {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  origin_page_header_sample_timer_.Stop();
  origin_page_header_sample_pending_url_ = GURL();
  origin_page_header_sampled_url_ = GURL();
  origin_page_header_color_.reset();
  UpdateOriginPageChromeColor(web_contents());
#endif
}

bool BraveBrowserView::IsPointInOriginTemporaryLinkHeader(
    const gfx::Point& point_in_widget) const {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  if (origin_temporary_link_view_ &&
      origin_temporary_link_view_->GetVisible() && GetWidget() &&
      origin_temporary_link_view_->GetWidget() == GetWidget()) {
    gfx::Point point_in_header(point_in_widget);
    views::View::ConvertPointFromWidget(origin_temporary_link_view_,
                                        &point_in_header);
    return origin_temporary_link_view_->HitTestPoint(point_in_header);
  }
#endif
  return false;
}

content::KeyboardEventProcessingResult BraveBrowserView::PreHandleKeyboardEvent(
    const input::NativeWebKeyboardEvent& event) {
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  if (event.GetType() == blink::WebInputEvent::Type::kRawKeyDown) {
    const ui::Accelerator accelerator =
        ui::GetAcceleratorFromNativeWebKeyboardEvent(event);
    const bool has_modifiers = accelerator.modifiers() != ui::EF_NONE;
    auto* contents = GetActiveWebContents();

    if (origin_temporary_link_view_) {
      if (contents && !contents->IsFocusedElementEditable()) {
        if (accelerator.modifiers() == ui::EF_NONE &&
            accelerator.key_code() >= ui::VKEY_1 &&
            accelerator.key_code() <= ui::VKEY_5 &&
            origin_temporary_link_view_->KeepInSpaceAtIndex(
                static_cast<size_t>(accelerator.key_code() - ui::VKEY_1))) {
          return content::KeyboardEventProcessingResult::HANDLED;
        }
        if (!has_modifiers && accelerator.key_code() == ui::VKEY_RETURN) {
          origin_temporary_link_view_->KeepSuggested();
          return content::KeyboardEventProcessingResult::HANDLED;
        }
        if (!has_modifiers && accelerator.key_code() == ui::VKEY_ESCAPE) {
          origin_temporary_link_view_->Discard();
          return content::KeyboardEventProcessingResult::HANDLED;
        }
        if (!has_modifiers && accelerator.key_code() == ui::VKEY_D) {
          origin_temporary_link_view_->Discard();
          return content::KeyboardEventProcessingResult::HANDLED;
        }
        if (!has_modifiers && accelerator.key_code() == ui::VKEY_S) {
          origin_temporary_link_view_->KeepInSplit();
          return content::KeyboardEventProcessingResult::HANDLED;
        }
        if (!has_modifiers && accelerator.key_code() == ui::VKEY_R) {
          origin_temporary_link_view_->ReplaceCurrentPage();
          return content::KeyboardEventProcessingResult::HANDLED;
        }
      }
      return BrowserView::PreHandleKeyboardEvent(event);
    }

    // Sigma's panel shortcut is Command+Left. Use the platform accelerator so
    // Linux and Windows receive Ctrl+Left, and keep it inactive while the user
    // is editing text in a page. Toolbar text fields never enter this
    // WebContents pre-handler, so normal cursor movement in the omnibox is
    // preserved as well.
    if (accelerator.modifiers() == ui::EF_PLATFORM_ACCELERATOR && contents &&
        !contents->IsFocusedElementEditable() &&
        accelerator.key_code() == ui::VKEY_LEFT) {
      chrome::ExecuteCommand(browser(), IDC_TOGGLE_VERTICAL_TABS_EXPANDED);
      return content::KeyboardEventProcessingResult::HANDLED;
    }

    if (accelerator.modifiers() == ui::EF_PLATFORM_ACCELERATOR && contents &&
        !contents->IsFocusedElementEditable() &&
        accelerator.key_code() >= ui::VKEY_1 &&
        accelerator.key_code() <= ui::VKEY_9) {
      if (auto* controller =
              browser()->GetFeatures().origin_space_controller()) {
        controller->SelectSpaceAtIndex(
            static_cast<size_t>(accelerator.key_code() - ui::VKEY_1));
      }
      return content::KeyboardEventProcessingResult::HANDLED;
    }

    if (accelerator.modifiers() == ui::EF_PLATFORM_ACCELERATOR && contents &&
        !contents->IsFocusedElementEditable() &&
        (accelerator.key_code() == ui::VKEY_UP ||
         accelerator.key_code() == ui::VKEY_DOWN)) {
      if (auto* controller =
              browser()->GetFeatures().origin_space_controller()) {
        controller->SelectAdjacentSpace(accelerator.key_code() ==
                                        ui::VKEY_DOWN);
      }
      return content::KeyboardEventProcessingResult::HANDLED;
    }

    if (accelerator.modifiers() == ui::EF_PLATFORM_ACCELERATOR && contents &&
        !contents->IsFocusedElementEditable() &&
        accelerator.key_code() == ui::VKEY_RIGHT) {
      if (chrome::IsCommandEnabled(browser(), IDC_BREAK_TILE)) {
        chrome::ExecuteCommand(browser(), IDC_BREAK_TILE);
      }
      return content::KeyboardEventProcessingResult::HANDLED;
    }

    if (!has_modifiers && accelerator.key_code() == ui::VKEY_ESCAPE &&
        origin_insert_mode_) {
      origin_insert_mode_ = false;
      return content::KeyboardEventProcessingResult::HANDLED;
    }

    if (!origin_insert_mode_ && !has_modifiers && contents &&
        !contents->IsFocusedElementEditable()) {
      if (accelerator.key_code() >= ui::VKEY_1 &&
          accelerator.key_code() <= ui::VKEY_9) {
        if (auto* controller =
                browser()->GetFeatures().origin_space_controller()) {
          controller->SelectSpaceAtIndex(
              static_cast<size_t>(accelerator.key_code() - ui::VKEY_1));
        }
        return content::KeyboardEventProcessingResult::HANDLED;
      }

      switch (accelerator.key_code()) {
        case ui::VKEY_I:
          origin_insert_mode_ = true;
          return content::KeyboardEventProcessingResult::HANDLED;
        case ui::VKEY_J:
        case ui::VKEY_DOWN: {
          auto* controller = browser()->GetFeatures().origin_space_controller();
          if (controller) {
            controller->SelectAdjacentTab(/*next=*/true);
          }
          return content::KeyboardEventProcessingResult::HANDLED;
        }
        case ui::VKEY_K:
        case ui::VKEY_UP: {
          auto* controller = browser()->GetFeatures().origin_space_controller();
          if (controller) {
            controller->SelectAdjacentTab(/*next=*/false);
          }
          return content::KeyboardEventProcessingResult::HANDLED;
        }
        case ui::VKEY_D: {
          base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
              FROM_HERE,
              base::BindOnce(&BraveBrowserView::CloseActiveOriginTabTree,
                             weak_ptr_.GetWeakPtr()));
          return content::KeyboardEventProcessingResult::HANDLED;
        }
        case ui::VKEY_Z:
          chrome::ExecuteCommand(browser(), IDC_RESTORE_TAB);
          return content::KeyboardEventProcessingResult::HANDLED;
        case ui::VKEY_R:
          chrome::ExecuteCommand(browser(), IDC_RELOAD);
          return content::KeyboardEventProcessingResult::HANDLED;
        case ui::VKEY_SPACE:
          ShowOriginQuickOpen(ui::VKEY_SPACE);
          return content::KeyboardEventProcessingResult::HANDLED;
        case ui::VKEY_O:
          ShowOriginQuickOpen(ui::VKEY_O);
          return content::KeyboardEventProcessingResult::HANDLED;
        case ui::VKEY_N:
          ShowOriginQuickOpen(ui::VKEY_N);
          return content::KeyboardEventProcessingResult::HANDLED;
        case ui::VKEY_P: {
          auto* model = browser()->tab_strip_model();
          const int active_index = model->active_index();
          if (active_index != TabStripModel::kNoTab) {
            model->SetTabPinned(active_index,
                                !model->IsTabPinned(active_index));
          }
          return content::KeyboardEventProcessingResult::HANDLED;
        }
        case ui::VKEY_M: {
          auto* controller = browser()->GetFeatures().origin_space_controller();
          auto* monitor = browser()->GetFeatures().origin_media_monitor();
          if (controller && monitor) {
            monitor->ToggleSpaceMuted(controller->active_space_id());
          }
          return content::KeyboardEventProcessingResult::HANDLED;
        }
        case ui::VKEY_W:
          ToggleOriginWidgetPanel();
          return content::KeyboardEventProcessingResult::HANDLED;
        case ui::VKEY_OEM_4:
          chrome::ExecuteCommand(browser(), IDC_BACK);
          return content::KeyboardEventProcessingResult::HANDLED;
        case ui::VKEY_OEM_6:
          chrome::ExecuteCommand(browser(), IDC_FORWARD);
          return content::KeyboardEventProcessingResult::HANDLED;
        case ui::VKEY_F:
          chrome::ExecuteCommand(browser(), IDC_TOGGLE_FOCUS_MODE);
          return content::KeyboardEventProcessingResult::HANDLED;
        default:
          break;
      }
    }
  }
#endif

  return BrowserView::PreHandleKeyboardEvent(event);
}

bool BraveBrowserView::AcceleratorPressed(const ui::Accelerator& accelerator) {
  int command_id = 0;
  const bool has_command =
      FindCommandIdForAccelerator(accelerator, &command_id);
#if BUILDFLAG(IS_BRAVE_ORIGIN_BRANDED)
  if (accelerator.modifiers() == ui::EF_NONE &&
      accelerator.key_code() == ui::VKEY_ESCAPE &&
      origin_temporary_link_view_) {
    origin_temporary_link_view_->Discard();
    return true;
  }
  // BrowserView registers Escape as a window-level accelerator so exclusive
  // access modes can see both press and release events. Handle Quick Open
  // before forwarding to BrowserView; otherwise the focused text field may
  // never receive the key event that normally dismisses the overlay.
  if (accelerator.modifiers() == ui::EF_NONE &&
      accelerator.key_code() == ui::VKEY_ESCAPE && origin_quick_open_view_ &&
      origin_quick_open_view_->GetVisible()) {
    HideOriginQuickOpen();
    return true;
  }

  if (!origin_temporary_link_view_ &&
      accelerator.modifiers() == ui::EF_PLATFORM_ACCELERATOR) {
    if (accelerator.key_code() == ui::VKEY_T && origin_quick_open_view_) {
      ShowOriginQuickOpen();
      return true;
    }
    if (accelerator.key_code() == ui::VKEY_K && origin_quick_open_view_) {
      ShowOriginCommander();
      return true;
    }
    if (accelerator.key_code() >= ui::VKEY_1 &&
        accelerator.key_code() <= ui::VKEY_9) {
      if (auto* controller =
              browser()->GetFeatures().origin_space_controller()) {
        controller->SelectSpaceAtIndex(
            static_cast<size_t>(accelerator.key_code() - ui::VKEY_1));
      }
      return true;
    }
  }
  if (has_command && command_id == IDC_NEW_TAB && origin_quick_open_view_) {
    ShowOriginQuickOpen();
    return true;
  }
#endif

  if (base::FeatureList::IsEnabled(tabs::kBraveSharedPinnedTabs) &&
      browser()->GetProfile()->GetPrefs()->GetBoolean(
          brave_tabs::kSharedPinnedTab)) {
    if (has_command && command_id == IDC_CLOSE_TAB) {
      auto* tab_strip_model = browser()->tab_strip_model();
      if (tab_strip_model->IsTabPinned(tab_strip_model->active_index())) {
        // Ignore CLOSE TAB command via accelerator if the tab is shared/dummy
        // pinned tab.
        return true;
      }
    }
  }
  return BrowserView::AcceleratorPressed(accelerator);
}

bool BraveBrowserView::IsInTabDragging() const {
  return browser_widget()->tab_drag_kind() == TabDragKind::kAllTabs;
}

void BraveBrowserView::ReadyToListenFullscreenChanges() {
  CHECK(browser_->GetFeatures().exclusive_access_manager());

  if (vertical_tab_strip_container_view_) {
    vertical_tab_strip_container_view_->vertical_tab_strip_region_view()
        ->ListenFullscreenChanges();
  }
}

void BraveBrowserView::StopListeningFullscreenChanges() {
  CHECK(browser_->GetFeatures().exclusive_access_manager());

  if (vertical_tab_strip_container_view_) {
    vertical_tab_strip_container_view_->vertical_tab_strip_region_view()
        ->StopListeningFullscreenChanges();
  }
}

void BraveBrowserView::HandleBrowserWindowMouseEvent(
    const ui::MouseEvent& event) {
  CHECK(event.type() == ui::EventType::kMouseMoved);

  // Use GetCursorScreenPoint() to get current mouse position in screen.
  // event.root_location_f() could not give in screen coordinate in some
  // situation.
  const gfx::PointF point_in_screen(
      display::Screen::Get()->GetCursorScreenPoint());

  if (sidebar_container_view_) {
    sidebar_container_view_->ShowSidebarOnMouseOver(point_in_screen);
  }

  if (vertical_tab_strip_container_view_ &&
      VerticalTabController::FromBrowser(browser())
          ->ShouldShowBraveVerticalTabs()) {
    vertical_tab_strip_container_view_->vertical_tab_strip_region_view()
        ->HandleMouseEvent(point_in_screen);
  }
}

bool BraveBrowserView::IsWebPanelContents(content::WebContents* contents) {
  if (!sidebar::IsWebPanelFeatureEnabled() || !contents) {
    return false;
  }

  if (auto* sidebar_controller = browser_->GetFeatures().sidebar_controller()) {
    if (auto* web_panel_controller =
            sidebar_controller->GetWebPanelController()) {
      return web_panel_controller->panel_contents() == contents;
    }
  }

  return false;
}

ClientFrameElementInfo BraveBrowserView::GetFrameElementInfo() const {
  ClientFrameElementInfo info = BrowserView::GetFrameElementInfo();
  if (VerticalTabController::FromBrowser(browser())
          ->ShouldShowBraveVerticalTabs()) {
    // In case of Brave vertical tabs, we don't want to show the tabstrip.
    info.tabstrip_preferred_height = 0;

#if BUILDFLAG(IS_WIN)
    // On Windows, we need to set |toolbar_minimum_height| to calculate
    // the correct caption button container height.
    // See BrowserFrameViewWin::TitlebarMaximizedVisualHeight().
    if (!VerticalTabController::FromBrowser(browser())
             ->ShouldShowWindowTitleForVerticalTabs()) {
      info.toolbar_minimum_height = toolbar_->GetMinimumSize().height();
    }
#endif
  }
  return info;
}

void BraveBrowserView::OnImmersiveFullscreenExited() {
  BrowserView::OnImmersiveFullscreenExited();
  tab_strip_placement_->UpdatePlacement();
}

void BraveBrowserView::OnImmersiveModeControllerDestroyed() {
  // When the immersive mode controller is destroyed during browser teardown,
  // ensure that top-reveal views are returned to their original and expected
  // placement in order to avoid violating view heirarchy assumptions in the
  // immersive fullscreen controller.
  if (focus_mode_top_overlay_) {
    focus_mode_top_overlay_->Deactivate();
  }
  BrowserView::OnImmersiveModeControllerDestroyed();
}

bool BraveBrowserView::IsSidebarVisible() const {
  return sidebar_container_view_ && sidebar_container_view_->IsSidebarVisible();
}

BraveBrowser* BraveBrowserView::GetBraveBrowser() const {
  return static_cast<BraveBrowser*>(browser_.get());
}

void BraveBrowserView::UpdateContentsCornerRadii(
    const gfx::RoundedCornersF& corner_radii) {
  GetBraveMultiContentsView()->UpdateContentsCornerRadii(corner_radii);
}

void BraveBrowserView::UpdateFocusModeState() {
  bool enabled = IsFocusModeEnabled(browser());

  // If the location bar is showing a warning (e.g. http, cert errors, etc.),
  // configure the browser view as if Focus Mode is not enabled.
  if (enabled && ShouldDisableFocusModeForActiveTab()) {
    enabled = false;
  }

  const bool effective_state_changed =
      !effective_focus_mode_state_initialized_ ||
      effective_focus_mode_enabled_ != enabled;
  effective_focus_mode_enabled_ = enabled;
  effective_focus_mode_state_initialized_ = true;

  const bool overlay_state_changed =
      focus_mode_top_overlay_ && focus_mode_top_overlay_->active() != enabled;
  if (overlay_state_changed) {
    if (enabled) {
      // Ensure that the overlay is at the end of the child list for correct
      // z-order rendering.
      ReorderChildView(focus_mode_top_overlay_, -1);
      focus_mode_top_overlay_->Activate();
    } else {
      focus_mode_top_overlay_->Deactivate();
    }
    EnsureFindBarHostViewIsLastChild();
    InvalidateLayout();
  }

  if (focus_mode_title_bar_view_) {
    focus_mode_title_bar_view_->SetVisible(enabled);
    focus_mode_title_bar_view_->SetTab(
        enabled ? browser()->tab_strip_model()->GetActiveTab() : nullptr);
  }

  const bool show_domain =
      enabled && features::kFocusModeUrlDisplay.Get() ==
                     features::FocusModeUrlDisplay::kMiniToolbar;
  if (show_domain != show_active_contents_domain_) {
    show_active_contents_domain_ = show_domain;
    GetBraveMultiContentsView()->OnShowActiveContentsDomainChanged();
  }

  // The toolbar is given extra horizontal padding in vertical tabs mode when
  // it is not hosted in the top overlay. Since the overlay's active state may
  // have changed, trigger an update of the horizontal padding.
  if (effective_state_changed || overlay_state_changed) {
    if (auto* toolbar_view = views::AsViewClass<BraveToolbarView>(toolbar())) {
      toolbar_view->UpdateHorizontalPadding();
    }
  }
}

bool BraveBrowserView::ShouldDisableFocusModeForActiveTab() const {
  auto* location_bar = GetLocationBar();
  if (!location_bar) {
    return true;
  }
  auto* model = location_bar->GetLocationBarModel();
  if (!model) {
    return true;
  }
  auto level = model->GetSecurityLevel();
  return level != security_state::SecurityLevel::SECURE;
}

void BraveBrowserView::StartTabCycling() {
  tab_cycling_event_handler_ = std::make_unique<TabCyclingEventHandler>(this);
}

void BraveBrowserView::StopTabCycling() {
  tab_cycling_event_handler_.reset();
  static_cast<BraveTabStripModel*>(browser()->tab_strip_model())
      ->StopMRUCycling();
}

BEGIN_METADATA(BraveBrowserView)
END_METADATA
