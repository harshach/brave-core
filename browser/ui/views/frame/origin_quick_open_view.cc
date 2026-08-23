/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/views/frame/origin_quick_open_view.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/hash/hash.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "brave/browser/ui/tabs/origin_space_controller.h"
#include "brave/browser/ui/views/frame/origin_site_identity.h"
#include "brave/browser/workspaces/workspace_service.h"
#include "brave/browser/workspaces/workspace_service_factory.h"
#include "brave/components/vector_icons/vector_icons.h"
#include "build/build_config.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/skia_paint_canvas.h"
#include "chrome/browser/autocomplete/chrome_autocomplete_provider_client.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/favicon/favicon_utils.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/image_fetcher/image_fetcher_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_key.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/favicon/core/fallback_url_util.h"
#include "components/favicon/core/favicon_service.h"
#include "components/favicon_base/favicon_types.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "components/image_fetcher/core/image_fetcher.h"
#include "components/image_fetcher/core/image_fetcher_service.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/omnibox/browser/autocomplete_classifier.h"
#include "components/omnibox/browser/autocomplete_controller_config.h"
#include "components/omnibox/browser/autocomplete_enums.h"
#include "components/omnibox/browser/autocomplete_input.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/omnibox/browser/autocomplete_match_type.h"
#include "components/omnibox/browser/autocomplete_result.h"
#include "components/search_engines/template_url.h"
#include "components/tabs/public/tab_interface.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/common/url_constants.h"
#include "extensions/common/extension_urls.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "skia/ext/image_operations.h"
#include "third_party/metrics_proto/omnibox_event.pb.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/l10n/time_format.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/gfx/range/range.h"
#include "ui/gfx/text_elider.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"

namespace {

constexpr int kPanelWidth = 660;
constexpr int kPanelCornerRadius = 14;
constexpr int kResultCornerRadius = 8;
constexpr int kResultRowHeight = 44;
constexpr size_t kPrimarySectionCapacity = 3;
constexpr size_t kSecondarySectionCapacity = 3;
constexpr size_t kTertiarySectionCapacity = 3;
constexpr size_t kMaxHistoryResults = 24;
constexpr size_t kMaxQueryHistoryResults = 100;
constexpr int64_t kMaxFaviconDownloadBytes = 1024 * 1024;
constexpr float kPanelBlurSigma = 32.0f;
constexpr float kPanelBackdropQuality = 0.40f;

constexpr char kImageFetcherUmaClientName[] = "OriginQuickOpenFavicon";

constexpr net::NetworkTrafficAnnotationTag kQuickOpenFaviconTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("origin_quick_open_favicon", R"(
      semantics {
        sender: "Origin Quick Open"
        description:
          "Fetches a website favicon for a URL suggestion shown while the "
          "user types in Origin Quick Open."
        trigger:
          "When the user types a website name or URL and no matching favicon "
          "is available in the local favicon database."
        data: "The URL of the website favicon."
        destination: WEBSITE
      }
      policy {
        cookies_allowed: NO
        setting:
          "This request is only made for a website suggestion derived from "
          "the user's current Quick Open input."
      })");

struct OriginQuickOpenColors {
  SkColor scrim;
  SkColor panel;
  SkColor panel_stroke;
  SkColor selected_result;
  SkColor hovered_result;
  SkColor selection_text;
  SkColor primary_text;
  SkColor secondary_text;
  SkColor tertiary_text;
  SkColor chip_background;
  SkColor current_chip_background;
  SkColor chip_hovered;
  SkColor footer_background;
  SkColor key_background;
};

OriginQuickOpenColors GetOriginQuickOpenColors(
    const ui::ColorProvider* color_provider) {
  const bool dark =
      !color_provider ||
      color_utils::IsDark(color_provider->GetColor(kColorToolbar));
  if (dark) {
    return {
        .scrim = SkColorSetARGB(0x80, 0x0A, 0x0D, 0x12),
        .panel = SkColorSetARGB(0xEB, 0x1C, 0x1F, 0x25),
        .panel_stroke = SkColorSetARGB(0x12, 0xFF, 0xFF, 0xFF),
        .selected_result = SkColorSetARGB(0x33, 0x15, 0x70, 0xEF),
        .hovered_result = SkColorSetARGB(0x0F, 0xFF, 0xFF, 0xFF),
        .selection_text = SkColorSetRGB(0xB2, 0xCC, 0xFF),
        .primary_text = SkColorSetRGB(0xF5, 0xF5, 0xF6),
        .secondary_text = SkColorSetRGB(0x94, 0x96, 0x9C),
        .tertiary_text = SkColorSetRGB(0x6B, 0x6E, 0x75),
        .chip_background = SkColorSetARGB(0x0F, 0xFF, 0xFF, 0xFF),
        .current_chip_background = SkColorSetARGB(0x12, 0xFF, 0xFF, 0xFF),
        .chip_hovered = SkColorSetARGB(0x1F, 0xFF, 0xFF, 0xFF),
        .footer_background = SkColorSetARGB(0x0A, 0xFF, 0xFF, 0xFF),
        .key_background = SkColorSetARGB(0x19, 0xFF, 0xFF, 0xFF),
    };
  }
  return {
      .scrim = SkColorSetARGB(0x5C, 0x18, 0x1D, 0x27),
      .panel = SkColorSetARGB(0xF7, 0xFF, 0xFF, 0xFF),
      .panel_stroke = SkColorSetARGB(0x1F, 0x18, 0x1D, 0x27),
      .selected_result = SkColorSetRGB(0xE8, 0xF0, 0xFE),
      .hovered_result = SkColorSetARGB(0x0A, 0x18, 0x1D, 0x27),
      .selection_text = SkColorSetRGB(0x16, 0x4C, 0x9B),
      .primary_text = SkColorSetRGB(0x18, 0x1D, 0x27),
      .secondary_text = SkColorSetRGB(0x53, 0x58, 0x62),
      .tertiary_text = SkColorSetRGB(0x71, 0x76, 0x80),
      .chip_background = SkColorSetARGB(0x0A, 0x18, 0x1D, 0x27),
      .current_chip_background = SkColorSetARGB(0x0D, 0x18, 0x1D, 0x27),
      .chip_hovered = SkColorSetARGB(0x14, 0x18, 0x1D, 0x27),
      .footer_background = SkColorSetARGB(0x08, 0x18, 0x1D, 0x27),
      .key_background = SkColorSetARGB(0x0D, 0x18, 0x1D, 0x27),
  };
}

constexpr std::array<SkColor, 5> kSpaceAccentColors = {
    SkColorSetRGB(0x15, 0x70, 0xEF), SkColorSetRGB(0x17, 0xB2, 0x6A),
    SkColorSetRGB(0xF7, 0x90, 0x09), SkColorSetRGB(0xF0, 0x44, 0x38),
    SkColorSetRGB(0x7A, 0x5A, 0xF8),
};

SkColor GetSpaceAccentColor(size_t index) {
  return kSpaceAccentColors[index % kSpaceAccentColors.size()];
}

gfx::FontList OriginQuickOpenFont(int size, gfx::Font::Weight weight) {
#if BUILDFLAG(IS_MAC)
  constexpr char kFamily[] = "SF Pro Text";
#elif BUILDFLAG(IS_WIN)
  constexpr char kFamily[] = "Segoe UI Variable";
#else
  constexpr char kFamily[] = "Inter";
#endif
  return gfx::FontList({kFamily}, gfx::Font::NORMAL, size, weight);
}

const gfx::VectorIcon& GetOriginQuickOpenSpaceIcon(std::string_view icon) {
  if (icon == kOriginSpaceIconHome) {
    return kLeoContainerPersonalIcon;
  }
  if (icon == kOriginSpaceIconWork) {
    return kLeoContainerWorkIcon;
  }
  if (icon == kOriginSpaceIconPlayground) {
    return kLeoRocketIcon;
  }
  if (icon == kOriginSpaceIconReading) {
    return kLeoReadingListIcon;
  }
  if (icon == kOriginSpaceIconTerminal) {
    return kLeoCodeIcon;
  }
  if (icon == kOriginSpaceIconMessages) {
    return kLeoContainerMessagingIcon;
  }
  if (icon == kOriginSpaceIconSchool) {
    return kLeoContainerSchoolIcon;
  }
  if (icon == kOriginSpaceIconShopping) {
    return kLeoContainerShoppingIcon;
  }
  if (icon == kOriginSpaceIconTravel) {
    return kLeoContainerTravelIcon;
  }
  return kLeoSpacesIcon;
}

bool IsUsefulPageURL(const GURL& url) {
  return url.is_valid() && url.SchemeIsHTTPOrHTTPS();
}

bool IsQuickOpenNavigableURL(const GURL& url) {
  return IsUsefulPageURL(url) ||
         (url.is_valid() &&
          (url.SchemeIs(content::kChromeUIScheme) ||
           url.SchemeIs(content::kBraveUIScheme)));
}

ui::ImageModel GetOriginFallbackFaviconModel(const GURL& url) {
  if (!IsUsefulPageURL(url)) {
    return favicon::GetDefaultFaviconModel();
  }
  // FaviconService only returns icons that have already reached the local
  // favicon database. Use Chromium's domain-derived color and monogram while
  // a typed/loading site is still resolving, then replace it asynchronously
  // with the real favicon in OnFaviconLoaded(). This avoids generic globes in
  // the exact moment Quick Open is meant to feel responsive.
  constexpr int kFaviconSize = 20;
  constexpr std::array<SkColor, 8> kFallbackColors = {
      SkColorSetRGB(0x15, 0x70, 0xEF), SkColorSetRGB(0x7A, 0x5A, 0xF8),
      SkColorSetRGB(0x07, 0x94, 0x55), SkColorSetRGB(0xF7, 0x90, 0x09),
      SkColorSetRGB(0xD9, 0x2D, 0x20), SkColorSetRGB(0x0E, 0x93, 0xA7),
      SkColorSetRGB(0xC1, 0x15, 0x74), SkColorSetRGB(0x41, 0x46, 0x51),
  };
  const SkColor accent = GetOriginKnownSiteAccent(url).value_or(
      kFallbackColors[base::PersistentHash(url.host()) %
                      kFallbackColors.size()]);

  SkBitmap bitmap;
  bitmap.allocN32Pixels(kFaviconSize, kFaviconSize, false);
  bitmap.eraseColor(SK_ColorTRANSPARENT);
  cc::SkiaPaintCanvas paint_canvas(bitmap);
  gfx::Canvas canvas(&paint_canvas, 1.0f);
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(accent);
  canvas.DrawRoundRect(gfx::RectF(0, 0, kFaviconSize, kFaviconSize), 4.0f,
                       flags);
  canvas.DrawStringRectWithFlags(
      favicon::GetFallbackIconText(url),
      OriginQuickOpenFont(10, gfx::Font::Weight::BOLD), SK_ColorWHITE,
      gfx::Rect(kFaviconSize, kFaviconSize), gfx::Canvas::TEXT_ALIGN_CENTER);
  return ui::ImageModel::FromImage(gfx::Image::CreateFrom1xBitmap(bitmap));
}

std::string NormalizeMatchText(std::u16string_view text) {
  std::u16string trimmed;
  base::TrimWhitespace(text, base::TrimPositions::TRIM_ALL, &trimmed);
  std::string normalized = base::ToLowerASCII(base::UTF16ToUTF8(trimmed));
  for (std::string_view scheme : {"https://", "http://"}) {
    if (normalized.starts_with(scheme)) {
      normalized.erase(0, scheme.size());
      break;
    }
  }
  if (normalized.starts_with("www.")) {
    normalized.erase(0, 4);
  }
  while (normalized.ends_with('/')) {
    normalized.pop_back();
  }
  return normalized;
}

std::string NormalizeHost(const GURL& url) {
  std::string host = base::ToLowerASCII(url.host());
  if (host.starts_with("www.")) {
    host.erase(0, 4);
  }
  return host;
}

size_t EditDistance(std::string_view left, std::string_view right) {
  std::vector<size_t> previous(right.size() + 1);
  std::vector<size_t> current(right.size() + 1);
  for (size_t index = 0; index <= right.size(); ++index) {
    previous[index] = index;
  }
  for (size_t left_index = 0; left_index < left.size(); ++left_index) {
    current[0] = left_index + 1;
    for (size_t right_index = 0; right_index < right.size(); ++right_index) {
      current[right_index + 1] =
          std::min({previous[right_index + 1] + 1, current[right_index] + 1,
                    previous[right_index] +
                        (left[left_index] == right[right_index] ? 0u : 1u)});
    }
    previous.swap(current);
  }
  return previous.back();
}

int ScoreCandidateText(std::string_view query, std::string_view candidate) {
  if (query.empty() || candidate.empty()) {
    return -1;
  }
  if (candidate == query) {
    return 12000;
  }
  if (candidate.starts_with(query)) {
    return 11000 - static_cast<int>(
                       std::min<size_t>(candidate.size() - query.size(), 500));
  }
  const size_t contained_at = candidate.find(query);
  if (contained_at != std::string_view::npos) {
    return 9000 - static_cast<int>(std::min<size_t>(contained_at * 20, 1000));
  }

  if (query.size() < 3 || query.size() > 64 || candidate.size() > 128) {
    return -1;
  }

  int best_score = -1;
  for (size_t token_start = 0; token_start < candidate.size();) {
    while (token_start < candidate.size() &&
           !base::IsAsciiAlphaNumeric(candidate[token_start])) {
      ++token_start;
    }
    size_t token_end = token_start;
    while (token_end < candidate.size() &&
           base::IsAsciiAlphaNumeric(candidate[token_end])) {
      ++token_end;
    }
    if (token_start == token_end) {
      break;
    }
    const std::string_view token =
        candidate.substr(token_start, token_end - token_start);
    const size_t distance = EditDistance(query, token);
    const size_t allowed_distance = std::max<size_t>(1, query.size() / 3);
    if (distance <= allowed_distance) {
      best_score = std::max(
          best_score,
          7000 - static_cast<int>(distance * 400) -
              static_cast<int>(std::min<size_t>(
                  token.size() > query.size() ? token.size() - query.size()
                                              : query.size() - token.size(),
                  500)));
    }
    token_start = token_end;
  }
  return best_score;
}

int ScoreSiteMatch(std::string_view query,
                   std::u16string_view title,
                   const GURL& url) {
  int score = ScoreCandidateText(query, NormalizeHost(url));
  const std::string host = NormalizeHost(url);
  const size_t first_dot = host.find('.');
  if (first_dot != std::string::npos) {
    score =
        std::max(score, ScoreCandidateText(query, host.substr(0, first_dot)));
  }
  score = std::max(score, ScoreCandidateText(query, NormalizeMatchText(title)));
  return score;
}

std::optional<GURL> BuildTypedDomainCandidate(std::u16string_view input) {
  std::u16string trimmed;
  base::TrimWhitespace(input, base::TrimPositions::TRIM_ALL, &trimmed);
  std::string typed = base::ToLowerASCII(base::UTF16ToUTF8(trimmed));
  if (typed.empty() || typed.find_first_of(" \t\r\n?#@") != std::string::npos) {
    return std::nullopt;
  }

  const bool has_scheme =
      typed.starts_with("http://") || typed.starts_with("https://");
  if (has_scheme) {
    GURL explicit_url(typed);
    return IsUsefulPageURL(explicit_url) ? std::optional<GURL>(explicit_url)
                                         : std::nullopt;
  }

  const size_t slash = typed.find('/');
  std::string host = typed.substr(0, slash);
  const std::string path =
      slash == std::string::npos ? std::string() : typed.substr(slash);
  if (host.starts_with("www.")) {
    host.erase(0, 4);
  }
  if (host.empty() || host.ends_with('.') ||
      host.find(':') != std::string::npos) {
    return std::nullopt;
  }

  GURL direct_url("https://" + host + path);
  if (!direct_url.is_valid()) {
    return std::nullopt;
  }
  if (direct_url.HostIsIPAddress() || host == "localhost" ||
      net::registry_controlled_domains::HostHasRegistryControlledDomain(
          host, net::registry_controlled_domains::EXCLUDE_UNKNOWN_REGISTRIES,
          net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES)) {
    return direct_url;
  }

  // A dot means the user is already expressing a hostname. Do not turn an
  // incomplete hostname such as "news.yc" into the unrelated
  // "news.yc.com". History, open tabs, and known-site matching can now rank
  // the intended host and provide inline completion.
  if (host.find('.') != std::string::npos) {
    return std::nullopt;
  }

  // Quick Open always keeps the web-search action visible, so a clean bare
  // hostname can safely offer its .com destination as the primary navigation
  // action. This is what lets "macrumors" fill to "macrumors.com" without
  // turning prose such as "mac rumors" into a guessed URL.
  if (host.find('.') == std::string::npos &&
      (host.size() < 2 || host.front() == '-' || host.back() == '-' ||
       !std::ranges::all_of(host, [](char character) {
         return base::IsAsciiAlphaNumeric(character) || character == '-';
       }))) {
    return std::nullopt;
  }
  GURL completed_url("https://" + host + ".com" + path);
  return completed_url.is_valid() ? std::optional<GURL>(completed_url)
                                  : std::nullopt;
}

std::u16string FormatElapsedTime(base::Time time) {
  if (time.is_null()) {
    return std::u16string();
  }
  return ui::TimeFormat::Simple(
      ui::TimeFormat::FORMAT_ELAPSED, ui::TimeFormat::LENGTH_LONG,
      std::max(base::TimeDelta(), base::Time::Now() - time));
}

}  // namespace

class OriginQuickOpenTextButton : public views::LabelButton {
  METADATA_HEADER(OriginQuickOpenTextButton, views::LabelButton)

 public:
  OriginQuickOpenTextButton(PressedCallback callback,
                            const std::u16string& text)
      : LabelButton(std::move(callback), text) {
    SetFocusBehavior(FocusBehavior::NEVER);
    SetRequestFocusOnPress(false);
    label()->SetSubpixelRenderingEnabled(false);
  }

  void SetOriginFont(const gfx::FontList& font) { label()->SetFontList(font); }

  void SetOriginSelected(bool selected) {
    selected_ = selected;
    RefreshStyle();
  }

 protected:
  void StateChanged(ButtonState old_state) override {
    LabelButton::StateChanged(old_state);
    RefreshStyle();
  }

  void OnThemeChanged() override {
    LabelButton::OnThemeChanged();
    RefreshStyle();
  }

 private:
  void RefreshStyle() {
    const auto colors = GetOriginQuickOpenColors(GetColorProvider());
    const bool hovered =
        GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    const SkColor foreground = selected_ ? SK_ColorWHITE
                               : hovered ? colors.primary_text
                                         : colors.tertiary_text;
    const SkColor background = selected_ ? SkColorSetRGB(0x15, 0x70, 0xEF)
                               : hovered ? colors.hovered_result
                                         : SK_ColorTRANSPARENT;
    SetEnabledTextColors(foreground);
    SetBackground(views::CreateRoundedRectBackground(background, 8));
  }

  bool selected_ = false;
};

BEGIN_METADATA(OriginQuickOpenTextButton)
END_METADATA

enum class OriginQuickOpenTextTone {
  kPrimary,
  kSecondary,
  kTertiary,
};

class OriginQuickOpenThemedLabel : public views::Label {
  METADATA_HEADER(OriginQuickOpenThemedLabel, views::Label)

 public:
  OriginQuickOpenThemedLabel(std::u16string text,
                             OriginQuickOpenTextTone tone,
                             bool key_background = false)
      : Label(std::move(text)), tone_(tone), key_background_(key_background) {
    SetSubpixelRenderingEnabled(false);
    RefreshStyle();
  }

  void OnThemeChanged() override {
    Label::OnThemeChanged();
    RefreshStyle();
  }

 private:
  void RefreshStyle() {
    const auto colors = GetOriginQuickOpenColors(GetColorProvider());
    switch (tone_) {
      case OriginQuickOpenTextTone::kPrimary:
        SetEnabledColor(colors.primary_text);
        break;
      case OriginQuickOpenTextTone::kSecondary:
        SetEnabledColor(colors.secondary_text);
        break;
      case OriginQuickOpenTextTone::kTertiary:
        SetEnabledColor(colors.tertiary_text);
        break;
    }
    if (key_background_) {
      SetBackground(
          views::CreateRoundedRectBackground(colors.key_background, 4));
    }
  }

  const OriginQuickOpenTextTone tone_;
  const bool key_background_;
};

BEGIN_METADATA(OriginQuickOpenThemedLabel)
END_METADATA

class OriginQuickOpenSpaceChip : public views::Button {
  METADATA_HEADER(OriginQuickOpenSpaceChip, views::Button)

 public:
  OriginQuickOpenSpaceChip(PressedCallback callback, bool current_space)
      : Button(std::move(callback)), current_space_(current_space) {
    SetFocusBehavior(FocusBehavior::NEVER);
    SetRequestFocusOnPress(false);
    SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(5, 9)));
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 7));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    icon_view_ = AddChildView(std::make_unique<views::ImageView>());
    icon_view_->SetPreferredSize(gfx::Size(14, 14));
    icon_view_->SetImageSize(gfx::Size(14, 14));

    name_label_ = AddChildView(std::make_unique<views::Label>());
    name_label_->SetSubpixelRenderingEnabled(false);
    name_label_->SetFontList(
        OriginQuickOpenFont(12, gfx::Font::Weight::MEDIUM));
    shortcut_label_ = AddChildView(std::make_unique<views::Label>());
    shortcut_label_->SetSubpixelRenderingEnabled(false);
    shortcut_label_->SetFontList(
        OriginQuickOpenFont(10, gfx::Font::Weight::SEMIBOLD));

    if (current_space_) {
      caret_view_ = AddChildView(std::make_unique<views::ImageView>());
      caret_view_->SetPreferredSize(gfx::Size(12, 12));
      caret_view_->SetImageSize(gfx::Size(12, 12));
    }
    RefreshStyle();
  }

  void SetSpace(const OriginSpaceMetadata& space,
                SkColor accent,
                std::u16string shortcut) {
    icon_ = space.icon;
    accent_ = accent;
    name_label_->SetText(base::UTF8ToUTF16(space.name));
    shortcut_label_->SetText(std::move(shortcut));
    shortcut_label_->SetVisible(!shortcut_label_->GetText().empty());
    SetAccessibleName(
        (current_space_ ? u"Current Space: " : u"Send to Space: ") +
        base::UTF8ToUTF16(space.name));
    RefreshStyle();
    InvalidateLayout();
  }

 protected:
  void StateChanged(ButtonState old_state) override {
    Button::StateChanged(old_state);
    RefreshStyle();
  }

  void OnThemeChanged() override {
    Button::OnThemeChanged();
    RefreshStyle();
  }

 private:
  void RefreshStyle() {
    const auto colors = GetOriginQuickOpenColors(GetColorProvider());
    const bool hovered =
        GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    const SkColor background =
        hovered ? colors.chip_hovered
                : (current_space_ ? colors.current_chip_background
                                  : colors.chip_background);
    SetBackground(views::CreateRoundedRectBackground(background, 8));
    name_label_->SetEnabledColor(colors.primary_text);
    shortcut_label_->SetEnabledColor(colors.tertiary_text);
    if (!icon_.empty()) {
      icon_view_->SetImage(ui::ImageModel::FromVectorIcon(
          GetOriginQuickOpenSpaceIcon(icon_),
          current_space_ ? colors.secondary_text : accent_, 14));
    }
    if (caret_view_) {
      caret_view_->SetImage(ui::ImageModel::FromVectorIcon(
          kLeoCaratDownIcon, colors.secondary_text, 12));
    }
  }

  const bool current_space_;
  std::string icon_;
  SkColor accent_ = SK_ColorTRANSPARENT;
  raw_ptr<views::ImageView> icon_view_ = nullptr;
  raw_ptr<views::Label> name_label_ = nullptr;
  raw_ptr<views::Label> shortcut_label_ = nullptr;
  raw_ptr<views::ImageView> caret_view_ = nullptr;
};

BEGIN_METADATA(OriginQuickOpenSpaceChip)
END_METADATA

// A compact omnibox result row. The text field deliberately retains keyboard
// focus while these rows expose a separate visual selection, matching command
// palette behavior rather than a traditional focus-traversal list.
class OriginQuickOpenResultButton : public views::Button {
  METADATA_HEADER(OriginQuickOpenResultButton, views::Button)

 public:
  explicit OriginQuickOpenResultButton(PressedCallback callback)
      : Button(std::move(callback)) {
    SetFocusBehavior(FocusBehavior::NEVER);
    SetRequestFocusOnPress(false);
    SetPreferredSize(gfx::Size(0, kResultRowHeight));
    SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(5, 12)));

    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 12));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    icon_view_ = AddChildView(std::make_unique<views::ImageView>());
    icon_view_->SetPreferredSize(gfx::Size(20, 20));
    icon_view_->SetImageSize(gfx::Size(20, 20));

    auto* text_column = AddChildView(std::make_unique<views::View>());
    auto* text_layout =
        text_column->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kVertical));
    text_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    layout->SetFlexForView(text_column, 1);

    title_label_ = text_column->AddChildView(std::make_unique<views::Label>());
    title_label_->SetSubpixelRenderingEnabled(false);
    title_label_->SetHorizontalAlignment(gfx::HorizontalAlignment::ALIGN_LEFT);
    title_label_->SetElideBehavior(gfx::ELIDE_TAIL);
    title_label_->SetFontList(
        OriginQuickOpenFont(13, gfx::Font::Weight::MEDIUM));

    subtitle_label_ =
        text_column->AddChildView(std::make_unique<views::Label>());
    subtitle_label_->SetSubpixelRenderingEnabled(false);
    subtitle_label_->SetHorizontalAlignment(
        gfx::HorizontalAlignment::ALIGN_LEFT);
    subtitle_label_->SetElideBehavior(gfx::ELIDE_TAIL);
    subtitle_label_->SetFontList(
        OriginQuickOpenFont(12, gfx::Font::Weight::NORMAL));

    badge_label_ = AddChildView(std::make_unique<views::Label>());
    badge_label_->SetSubpixelRenderingEnabled(false);
    badge_label_->SetFontList(
        OriginQuickOpenFont(11, gfx::Font::Weight::SEMIBOLD));
    badge_label_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(2, 6)));

    enter_key_label_ = AddChildView(std::make_unique<views::Label>(u"↵"));
    enter_key_label_->SetSubpixelRenderingEnabled(false);
    enter_key_label_->SetFontList(
        OriginQuickOpenFont(10, gfx::Font::Weight::SEMIBOLD));
    enter_key_label_->SetBorder(
        views::CreateEmptyBorder(gfx::Insets::TLBR(1, 5, 1, 5)));
    enter_key_label_->SetVisible(false);

    RefreshRowStyle();
  }

  void SetResult(const std::u16string& title,
                 const std::u16string& subtitle,
                 const std::u16string& badge,
                 const ui::ImageModel& icon_model,
                 const gfx::VectorIcon* icon,
                 bool persistent_badge = false) {
    title_label_->SetText(title);
    subtitle_label_->SetText(subtitle);
    subtitle_label_->SetVisible(!subtitle.empty());
    std::u16string visible_badge = badge;
    has_enter_key_ = visible_badge.ends_with(u"  ↵");
    if (has_enter_key_) {
      visible_badge.resize(visible_badge.size() - 3);
    }
    badge_label_->SetText(visible_badge);
    has_badge_ = !visible_badge.empty();
    persistent_badge_ = persistent_badge;
    icon_model_ = icon_model;
    icon_ = icon;
    icon_view_->SetVisible(!icon_model_.IsEmpty() || icon_ != nullptr);
    std::u16string accessible_name =
        subtitle.empty() ? title : title + u", " + subtitle;
    if (!badge.empty()) {
      accessible_name.append(u", ").append(badge);
    }
    SetAccessibleName(accessible_name);
    RefreshRowStyle();
  }

  void SetPaletteSelected(bool selected) {
    if (palette_selected_ == selected) {
      return;
    }
    palette_selected_ = selected;
    RefreshRowStyle();
  }

 protected:
  void StateChanged(ButtonState old_state) override {
    Button::StateChanged(old_state);
    RefreshRowStyle();
  }

  void OnThemeChanged() override {
    Button::OnThemeChanged();
    RefreshRowStyle();
  }

 private:
  void RefreshRowStyle() {
    const auto colors = GetOriginQuickOpenColors(GetColorProvider());
    const bool hovered =
        GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    const SkColor background = palette_selected_ ? colors.selected_result
                               : hovered         ? colors.hovered_result
                                                 : SK_ColorTRANSPARENT;
    SetBackground(
        views::CreateRoundedRectBackground(background, kResultCornerRadius));
    title_label_->SetEnabledColor(colors.primary_text);
    subtitle_label_->SetEnabledColor(colors.secondary_text);
    badge_label_->SetEnabledColor(palette_selected_ ? colors.selection_text
                                                    : colors.secondary_text);
    badge_label_->SetBorder(
        persistent_badge_
            ? views::CreateRoundedRectBorder(1, 5, colors.tertiary_text)
            : views::CreateEmptyBorder(gfx::Insets::VH(2, 6)));
    badge_label_->SetVisible(has_badge_);
    enter_key_label_->SetEnabledColor(colors.primary_text);
    enter_key_label_->SetBackground(
        views::CreateRoundedRectBackground(colors.key_background, 5));
    enter_key_label_->SetVisible(has_enter_key_ && palette_selected_);
    if (!icon_model_.IsEmpty()) {
      icon_view_->SetImage(icon_model_);
    } else if (icon_) {
      icon_view_->SetImage(ui::ImageModel::FromVectorIcon(
          *icon_,
          palette_selected_ ? colors.selection_text : colors.secondary_text,
          18));
    }
  }

  bool palette_selected_ = false;
  bool has_badge_ = false;
  bool persistent_badge_ = false;
  ui::ImageModel icon_model_;
  raw_ptr<const gfx::VectorIcon> icon_ = nullptr;
  raw_ptr<views::ImageView> icon_view_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::Label> subtitle_label_ = nullptr;
  raw_ptr<views::Label> badge_label_ = nullptr;
  raw_ptr<views::Label> enter_key_label_ = nullptr;
  bool has_enter_key_ = false;
};

BEGIN_METADATA(OriginQuickOpenResultButton)
END_METADATA

BEGIN_METADATA(OriginQuickOpenView)
END_METADATA

OriginQuickOpenView::OriginQuickOpenView(
    Browser* browser,
    SubmitCallback submit_callback,
    base::RepeatingClosure command_callback,
    base::RepeatingClosure close_callback)
    : browser_(browser),
      profile_(browser ? browser->GetProfile() : nullptr),
      submit_callback_(std::move(submit_callback)),
      close_callback_(std::move(close_callback)) {
  CHECK(browser_);
  CHECK(profile_);

  autocomplete_controller_ = std::make_unique<AutocompleteController>(
      std::make_unique<ChromeAutocompleteProviderClient>(profile_),
      AutocompleteControllerConfig{
          .provider_types = AutocompleteClassifier::DefaultOmniboxProviders(),
          .unscoped_open_tab_suggestions = true});
  autocomplete_controller_->AddObserver(this);

  SetVisible(false);
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);

  panel_ = AddChildView(std::make_unique<views::View>());
  panel_->SetPaintToLayer();
  panel_->layer()->SetFillsBoundsOpaquely(false);
  panel_->layer()->SetRoundedCornerRadius(
      gfx::RoundedCornersF(kPanelCornerRadius));
  panel_->layer()->SetBackgroundBlur(kPanelBlurSigma);
  panel_->layer()->SetBackdropFilterQuality(kPanelBackdropQuality);
  panel_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::TLBR(14, 18, 12, 18), 4));

  auto* search_row = panel_->AddChildView(std::make_unique<views::View>());
  search_row->SetPreferredSize(gfx::Size(0, 54));
  auto* search_layout =
      search_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::TLBR(0, 14, 0, 8), 10));
  search_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  search_icon_ = search_row->AddChildView(std::make_unique<views::ImageView>());
  search_icon_->SetPreferredSize(gfx::Size(18, 18));
  search_icon_->SetImageSize(gfx::Size(18, 18));

  search_field_ =
      search_row->AddChildView(std::make_unique<views::Textfield>());
  search_field_->set_controller(this);
  search_field_->SetPlaceholderText(u"Search or ask a question…");
  search_field_->SetPlaceholderTextColorId(ui::kColorSysOnSurfaceSubtle);
  search_field_->SetTextColorId(ui::kColorSysOnSurface);
  search_field_->SetBackgroundEnabled(false);
  search_field_->SetBorder(views::CreateEmptyBorder(gfx::Insets()));
  search_field_->SetFontList(
      OriginQuickOpenFont(17, gfx::Font::Weight::NORMAL));
  search_layout->SetFlexForView(search_field_, 1);

  current_space_chip_ =
      search_row->AddChildView(std::make_unique<OriginQuickOpenSpaceChip>(
          views::Button::PressedCallback(), /*current_space=*/true));

  const auto add_section = [this](size_t section, size_t capacity) {
    auto* label =
        panel_->AddChildView(std::make_unique<views::Label>(u"Top Hits"));
    label->SetSubpixelRenderingEnabled(false);
    label->SetHorizontalAlignment(gfx::HorizontalAlignment::ALIGN_LEFT);
    label->SetFontList(OriginQuickOpenFont(11, gfx::Font::Weight::SEMIBOLD));
    label->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(6, 4, 0, 4)));
    section_labels_[section] = label;

    for (size_t index = 0; index < capacity; ++index) {
      auto* row =
          panel_->AddChildView(std::make_unique<OriginQuickOpenResultButton>(
              base::BindRepeating(&OriginQuickOpenView::OnResultPressed,
                                  base::Unretained(this), section, index)));
      row->SetVisible(false);
      section_rows_[section].push_back(row);
    }
  };

  add_section(/*section=*/0, kPrimarySectionCapacity);

  empty_state_label_ = panel_->AddChildView(std::make_unique<views::Label>(
      u"Your frequently used pages will appear here."));
  empty_state_label_->SetSubpixelRenderingEnabled(false);
  empty_state_label_->SetHorizontalAlignment(
      gfx::HorizontalAlignment::ALIGN_LEFT);
  empty_state_label_->SetFontList(
      OriginQuickOpenFont(13, gfx::Font::Weight::NORMAL));
  empty_state_label_->SetBorder(
      views::CreateEmptyBorder(gfx::Insets::TLBR(4, 12, 8, 12)));

  command_row_ =
      panel_->AddChildView(std::make_unique<OriginQuickOpenResultButton>(
          std::move(command_callback)));
  command_row_->SetResult(u"Search all Brave Commands", u"", u"⌘  K",
                          ui::ImageModel(), &kLeoSearchIcon,
                          /*persistent_badge=*/true);

  add_section(/*section=*/1, kSecondarySectionCapacity);
  add_section(/*section=*/2, kTertiarySectionCapacity);

  send_to_space_label_ =
      panel_->AddChildView(std::make_unique<views::Label>(u"Send to space"));
  send_to_space_label_->SetSubpixelRenderingEnabled(false);
  send_to_space_label_->SetHorizontalAlignment(
      gfx::HorizontalAlignment::ALIGN_LEFT);
  send_to_space_label_->SetFontList(
      OriginQuickOpenFont(11, gfx::Font::Weight::SEMIBOLD));
  send_to_space_label_->SetBorder(
      views::CreateEmptyBorder(gfx::Insets::TLBR(8, 4, 2, 4)));

  send_to_space_container_ =
      panel_->AddChildView(std::make_unique<views::View>());
  send_to_space_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets::TLBR(0, 4, 4, 4),
      6));

  shortcuts_footer_ = panel_->AddChildView(std::make_unique<views::View>());
  auto* footer_layout =
      shortcuts_footer_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::TLBR(7, 8, 7, 8), 12));
  footer_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  const auto add_shortcut = [this](std::u16string key,
                                   std::u16string description) {
    auto* group =
        shortcuts_footer_->AddChildView(std::make_unique<views::View>());
    auto* group_layout =
        group->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 6));
    group_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    auto* key_label =
        group->AddChildView(std::make_unique<OriginQuickOpenThemedLabel>(
            std::move(key), OriginQuickOpenTextTone::kPrimary,
            /*key_background=*/true));
    key_label->SetFontList(
        OriginQuickOpenFont(10, gfx::Font::Weight::SEMIBOLD));
    key_label->SetBorder(
        views::CreateEmptyBorder(gfx::Insets::TLBR(1, 5, 1, 5)));
    auto* description_label =
        group->AddChildView(std::make_unique<OriginQuickOpenThemedLabel>(
            std::move(description), OriginQuickOpenTextTone::kSecondary));
    description_label->SetFontList(
        OriginQuickOpenFont(11, gfx::Font::Weight::NORMAL));
  };
  add_shortcut(u"J K", u"Move");
  add_shortcut(u"↵", u"Open");
  add_shortcut(u"R", u"Replace page");
  add_shortcut(u"S", u"Split screen");
  add_shortcut(u"1–5", u"Send to space");
  auto* footer_spacer =
      shortcuts_footer_->AddChildView(std::make_unique<views::View>());
  footer_layout->SetFlexForView(footer_spacer, 1);
  auto* escape_label = shortcuts_footer_->AddChildView(
      std::make_unique<OriginQuickOpenThemedLabel>(
          u"Esc", OriginQuickOpenTextTone::kTertiary));
  escape_label->SetFontList(OriginQuickOpenFont(11, gfx::Font::Weight::NORMAL));

  RefreshTheme();
  UpdateResultRows();
}

OriginQuickOpenView::~OriginQuickOpenView() {
  task_tracker_.TryCancelAll();
  favicon_task_tracker_.TryCancelAll();
  if (search_field_) {
    search_field_->set_controller(nullptr);
  }
  if (autocomplete_controller_) {
    autocomplete_controller_->RemoveObserver(this);
    autocomplete_controller_->Stop(AutocompleteStopReason::kClobbered);
  }
}

void OriginQuickOpenView::ShowAndFocus(
    std::optional<ui::KeyboardCode> activation_key) {
  task_tracker_.TryCancelAll();
  autocomplete_controller_->Stop(AutocompleteStopReason::kClobbered);
  query_results_ = {};
  zero_state_results_ = {};
  displayed_results_ = {};
  open_tab_results_.clear();
  history_results_.clear();
  query_history_results_.clear();
  visible_results_.clear();
  user_input_.clear();
  selected_result_ = 0;
  selected_disposition_ = OriginQuickOpenDisposition::kNewPage;
  UpdateDispositionButtons();
  activation_key_to_suppress_.reset();
  suppress_inline_autocomplete_ = false;
  result_navigation_active_ = false;
  updating_inline_autocomplete_ = true;
  search_field_->SetText(std::u16string());
  updating_inline_autocomplete_ = false;
  activation_key_to_suppress_ = activation_key;
  SetVisible(true);
  search_field_->RequestFocus();
  RebuildSpaceControls();
  LoadZeroStateData();
}

void OriginQuickOpenView::Dismiss() {
  task_tracker_.TryCancelAll();
  autocomplete_controller_->Stop(AutocompleteStopReason::kClobbered);
  activation_key_to_suppress_.reset();
  SetVisible(false);
}

void OriginQuickOpenView::ContentsChanged(views::Textfield* sender,
                                          const std::u16string& new_contents) {
  if (sender != search_field_ || updating_inline_autocomplete_) {
    return;
  }

  // On some platforms the character event belonging to the browser-level
  // shortcut arrives after focus has moved into this text field. Remove only
  // that initial activation character; subsequent typing remains untouched.
  if (activation_key_to_suppress_) {
    const bool is_open_shortcut_character =
        (*activation_key_to_suppress_ == ui::VKEY_O &&
         (new_contents == u"o" || new_contents == u"O")) ||
        (*activation_key_to_suppress_ == ui::VKEY_N &&
         (new_contents == u"n" || new_contents == u"N")) ||
        (*activation_key_to_suppress_ == ui::VKEY_SPACE &&
         new_contents == u" ");
    activation_key_to_suppress_.reset();
    if (is_open_shortcut_character) {
      user_input_.clear();
      updating_inline_autocomplete_ = true;
      search_field_->SetText(std::u16string());
      updating_inline_autocomplete_ = false;
      selected_result_ = 0;
      LoadZeroStateData();
      return;
    }
  }

  const bool removed_only_inline_completion =
      suppress_inline_autocomplete_ && new_contents == user_input_;
  user_input_ = new_contents;
  result_navigation_active_ = false;
  if (!removed_only_inline_completion) {
    suppress_inline_autocomplete_ = false;
  }
  selected_result_ = 0;
  StartAutocomplete(user_input_);
}

bool OriginQuickOpenView::HandleKeyEvent(views::Textfield* sender,
                                         const ui::KeyEvent& key_event) {
  if (sender != search_field_) {
    return false;
  }

  if (activation_key_to_suppress_) {
    if (key_event.key_code() == *activation_key_to_suppress_) {
      // Keep suppression armed until key-up so a platform text/IME event that
      // follows key-down can still be recognized by ContentsChanged().
      if (key_event.type() == ui::EventType::kKeyReleased) {
        activation_key_to_suppress_.reset();
      }
      return true;
    }
    if (key_event.type() == ui::EventType::kKeyPressed) {
      activation_key_to_suppress_.reset();
    }
  }

  if (key_event.type() != ui::EventType::kKeyPressed) {
    return false;
  }

  const gfx::Range selected_range = search_field_->GetSelectedRange();
  const bool has_inline_completion =
      !selected_range.is_empty() &&
      selected_range.GetMin() == user_input_.size() &&
      selected_range.GetMax() == search_field_->GetText().size();
  if (has_inline_completion && (key_event.key_code() == ui::VKEY_BACK ||
                                key_event.key_code() == ui::VKEY_DELETE)) {
    suppress_inline_autocomplete_ = true;
  } else if (has_inline_completion && (key_event.key_code() == ui::VKEY_RIGHT ||
                                       key_event.key_code() == ui::VKEY_END ||
                                       key_event.key_code() == ui::VKEY_TAB)) {
    user_input_ = search_field_->GetText();
    suppress_inline_autocomplete_ = false;
    search_field_->SetSelectedRange(gfx::Range(user_input_.size()));
    StartAutocomplete(user_input_);
    return true;
  }

  const bool command_shortcut =
      result_navigation_active_ || key_event.IsControlDown() ||
      key_event.IsCommandDown();
  switch (key_event.key_code()) {
    case ui::VKEY_ESCAPE:
      close_callback_.Run();
      return true;
    case ui::VKEY_RETURN:
      Submit(GetEffectiveDisposition());
      return true;
    case ui::VKEY_DOWN:
      if (!visible_results_.empty()) {
        SelectResult(result_navigation_active_
                         ? (selected_result_ + 1) % visible_results_.size()
                         : selected_result_);
      }
      result_navigation_active_ = true;
      return true;
    case ui::VKEY_UP:
      if (!visible_results_.empty()) {
        SelectResult(result_navigation_active_
                         ? (selected_result_ + visible_results_.size() - 1) %
                               visible_results_.size()
                         : selected_result_);
      }
      result_navigation_active_ = true;
      return true;
    case ui::VKEY_R:
      if (!command_shortcut) {
        return false;
      }
      Submit(OriginQuickOpenDisposition::kReplace);
      return true;
    case ui::VKEY_S:
      if (!command_shortcut) {
        return false;
      }
      Submit(OriginQuickOpenDisposition::kSplit);
      return true;
    case ui::VKEY_J:
      if (!command_shortcut) {
        return false;
      }
      if (!visible_results_.empty()) {
        SelectResult((selected_result_ + 1) % visible_results_.size());
      }
      result_navigation_active_ = true;
      return true;
    case ui::VKEY_K:
      if (!command_shortcut) {
        return false;
      }
      if (!visible_results_.empty()) {
        SelectResult((selected_result_ + visible_results_.size() - 1) %
                     visible_results_.size());
      }
      result_navigation_active_ = true;
      return true;
    default:
      if (HasQuery() && key_event.key_code() >= ui::VKEY_1 &&
          key_event.key_code() <= ui::VKEY_5) {
        SubmitToSpace(
            static_cast<size_t>(key_event.key_code() - ui::VKEY_1));
        return true;
      }
      return false;
  }
}

void OriginQuickOpenView::OnResultChanged(AutocompleteController* controller,
                                          bool default_match_changed) {
  CHECK_EQ(controller, autocomplete_controller_.get());
  RebuildResults();
}

void OriginQuickOpenView::Layout(PassKey) {
  const gfx::Rect available = GetContentsBounds();
  const int panel_width =
      std::min(kPanelWidth, std::max(0, available.width() - 48));
  const int desired_height = panel_->GetPreferredSize().height();
  const int panel_height =
      std::min(desired_height, std::max(0, available.height() - 24));
  const int free_height = std::max(0, available.height() - panel_height);
  // Sigma keeps the palette on a stable visual shelf. Compact query results
  // sit 64dp from the top while the richer zero state grows toward the bottom.
  const int panel_y = available.y() + std::clamp(free_height - 10, 12, 64);
  panel_->SetBounds(available.x() + (available.width() - panel_width) / 2,
                    panel_y, panel_width, panel_height);
}

void OriginQuickOpenView::OnThemeChanged() {
  views::View::OnThemeChanged();
  RefreshTheme();
}

void OriginQuickOpenView::RefreshTheme() {
  const auto colors = GetOriginQuickOpenColors(GetColorProvider());
  SetBackground(views::CreateSolidBackground(colors.scrim));
  panel_->SetBackground(
      views::CreateRoundedRectBackground(colors.panel, kPanelCornerRadius));
  panel_->SetBorder(views::CreateRoundedRectBorder(1, kPanelCornerRadius,
                                                   colors.panel_stroke));
  for (views::Label* section_label : section_labels_) {
    if (section_label) {
      section_label->SetEnabledColor(colors.tertiary_text);
    }
  }
  empty_state_label_->SetEnabledColor(colors.secondary_text);
  send_to_space_label_->SetEnabledColor(colors.tertiary_text);
  shortcuts_footer_->SetBackground(
      views::CreateRoundedRectBackground(colors.footer_background, 8));
  UpdateSearchIcon();
}

bool OriginQuickOpenView::OnMousePressed(const ui::MouseEvent& event) {
  if (!panel_->bounds().Contains(event.location())) {
    close_callback_.Run();
  }
  return true;
}

void OriginQuickOpenView::Submit(OriginQuickOpenDisposition disposition) {
  if (selected_result_ < visible_results_.size()) {
    const auto [section, index] = visible_results_[selected_result_];
    if (section < displayed_results_.size() &&
        index < displayed_results_[section].size()) {
      SubmitResult(displayed_results_[section][index], disposition);
      return;
    }
  }

  OriginQuickOpenSelection selection;
  selection.input = user_input_;
  submit_callback_.Run(std::move(selection), disposition);
}

void OriginQuickOpenView::SubmitResult(const Result& result,
                                       OriginQuickOpenDisposition disposition) {
  OriginQuickOpenSelection selection;
  selection.input = user_input_;
  selection.destination_url = result.destination_url;
  selection.space_id = result.space_id;
  selection.switch_to_tab = result.switch_to_tab;
  submit_callback_.Run(std::move(selection), disposition);
}

void OriginQuickOpenView::SubmitToSpace(size_t space_index) {
  if (space_index >= send_to_space_ids_.size()) {
    return;
  }

  OriginQuickOpenSelection selection;
  selection.input = user_input_;
  selection.destination_space_id = send_to_space_ids_[space_index];
  if (selected_result_ < visible_results_.size()) {
    const auto [section, index] = visible_results_[selected_result_];
    if (section < displayed_results_.size() &&
        index < displayed_results_[section].size()) {
      const Result& result = displayed_results_[section][index];
      selection.destination_url = result.destination_url;
      selection.space_id = result.space_id;
      // Sending is an explicit request to create the selected destination in
      // another Space, even when the top hit is already open elsewhere.
      selection.switch_to_tab = false;
    }
  }
  submit_callback_.Run(std::move(selection),
                       OriginQuickOpenDisposition::kNewPage);
}

void OriginQuickOpenView::SetDisposition(
    OriginQuickOpenDisposition disposition) {
  selected_disposition_ = selected_disposition_ == disposition
                              ? OriginQuickOpenDisposition::kNewPage
                              : disposition;
  UpdateDispositionButtons();
}

OriginQuickOpenDisposition OriginQuickOpenView::GetEffectiveDisposition()
    const {
  return selected_disposition_;
}

void OriginQuickOpenView::UpdateDispositionButtons() {
  const OriginQuickOpenDisposition disposition = GetEffectiveDisposition();
  if (replace_button_) {
    replace_button_->SetOriginSelected(disposition ==
                                       OriginQuickOpenDisposition::kReplace);
  }
  if (split_button_) {
    split_button_->SetOriginSelected(disposition ==
                                     OriginQuickOpenDisposition::kSplit);
  }
}

void OriginQuickOpenView::OnResultPressed(size_t section, size_t index) {
  if (section >= displayed_results_.size() ||
      index >= displayed_results_[section].size()) {
    return;
  }
  const auto selected =
      std::ranges::find(visible_results_, std::pair(section, index));
  if (selected != visible_results_.end()) {
    SelectResult(static_cast<size_t>(selected - visible_results_.begin()));
  }
  SubmitResult(displayed_results_[section][index], GetEffectiveDisposition());
}

void OriginQuickOpenView::StartAutocomplete(const std::u16string& input) {
  std::u16string trimmed;
  base::TrimWhitespace(input, base::TrimPositions::TRIM_ALL, &trimmed);
  if (trimmed.empty()) {
    autocomplete_controller_->Stop(AutocompleteStopReason::kClobbered);
    query_results_ = {};
    LoadZeroStateData();
    return;
  }

  AutocompleteInput autocomplete_input(
      input, input.size(), metrics::OmniboxEventProto::OTHER,
      autocomplete_controller_->autocomplete_provider_client()
          ->GetSchemeClassifier());
  autocomplete_input.set_prevent_inline_autocomplete(false);
  autocomplete_controller_->Start(autocomplete_input);
  QueryHistoryForInput(trimmed);
  // Provider updates can arrive asynchronously. Build the deterministic
  // direct-site fallback immediately so typing a known destination such as
  // "reddit" never leaves a lone Brave Search row on screen while waiting.
  RebuildResults();
}

void OriginQuickOpenView::RebuildResults() {
  if (!HasQuery()) {
    return;
  }

  const AutocompleteResult& autocomplete_results =
      autocomplete_controller_->result();
  query_results_ = {};
  struct RankedResult {
    Result result;
    int score = 0;
  };
  std::vector<RankedResult> navigation_results;
  std::map<std::string, size_t> navigation_indices;
  std::optional<Result> exact_search_result;
  std::vector<RankedResult> search_suggestion_results;
  std::set<std::string> search_suggestion_keys;

  std::u16string trimmed_input;
  base::TrimWhitespace(user_input_, base::TrimPositions::TRIM_ALL,
                       &trimmed_input);
  const std::string normalized_input = NormalizeMatchText(trimmed_input);

  const auto find_open_tab = [this](const GURL& url) -> const Result* {
    const auto open_tab = std::ranges::find_if(
        open_tab_results_, [&url](const TimedResult& candidate) {
          return candidate.result.destination_url == url;
        });
    return open_tab == open_tab_results_.end() ? nullptr : &open_tab->result;
  };

  const auto add_navigation_result = [&navigation_results, &navigation_indices](
                                         Result result, int score) {
    if (!IsQuickOpenNavigableURL(result.destination_url) || score < 0) {
      return;
    }
    // Treat the conventional www alias as one destination. Otherwise a bare
    // partial query can show both reddit.com and www.reddit.com while crowding
    // the useful history and search alternatives out of the three-row shelf.
    std::string key = NormalizeHost(result.destination_url);
    key.append(result.destination_url.path());
    key.push_back('?');
    key.append(result.destination_url.query());
    const auto found = navigation_indices.find(key);
    if (found == navigation_indices.end()) {
      navigation_indices.emplace(key, navigation_results.size());
      navigation_results.push_back(RankedResult{std::move(result), score});
      return;
    }

    RankedResult& existing = navigation_results[found->second];
    const bool promotes_open_page =
        result.switch_to_tab && !existing.result.switch_to_tab;
    if (promotes_open_page || score > existing.score) {
      existing = RankedResult{std::move(result), score};
    }
  };

  // An already-open page is the most useful interpretation of a close match:
  // Enter switches to it instead of creating a duplicate page. Match against
  // both the visible title and hostname so "red" finds an open Reddit path.
  for (const TimedResult& open_tab : open_tab_results_) {
    const int match_score =
        ScoreSiteMatch(normalized_input, open_tab.result.title,
                       open_tab.result.destination_url);
    if (match_score >= 0) {
      Result result = open_tab.result;
      result.badge = u"Switch  ›";
      add_navigation_result(std::move(result), match_score + 50000);
    }
  }

  const auto add_history_matches =
      [&](const std::vector<TimedResult>& history_results) {
        for (const TimedResult& history_result : history_results) {
          Result result = history_result.result;
          const int match_score = ScoreSiteMatch(
              normalized_input, result.title, result.destination_url);
          if (match_score < 0) {
            continue;
          }
          const bool host_prefix =
              NormalizeHost(result.destination_url).starts_with(
                  normalized_input);
          if (const Result* open_tab = find_open_tab(result.destination_url)) {
            result = *open_tab;
            result.badge = u"Switch  ›";
          } else {
            std::u16string elapsed = result.subtitle;
            if (elapsed.starts_with(u"— ")) {
              elapsed.erase(0, 2);
            }
            result.subtitle.clear();
            result.badge = elapsed.empty() ? u"History"
                                           : u"History · " + elapsed;
            RequestFavicon(result.destination_url);
          }
          add_navigation_result(std::move(result),
                                match_score + (host_prefix ? 10000 : 2000));
        }
      };
  add_history_matches(history_results_);
  add_history_matches(query_history_results_);

  for (const AutocompleteMatch& match : autocomplete_results) {
    if (!match.destination_url.is_valid()) {
      continue;
    }

    const bool is_search = AutocompleteMatch::IsSearchType(match.type);

    Result result;
    result.destination_url = match.destination_url;
    result.is_search = is_search;
    result.icon = &match.GetVectorIcon(/*is_bookmark=*/false);

    if (is_search) {
      result.title =
          match.fill_into_edit.empty() ? match.contents : match.fill_into_edit;
      result.subtitle = match.description.empty()
                            ? u"— search the web"
                            : u"— on " + match.description;
      result.badge = u"Search";
      if (match.type == AutocompleteMatchType::SEARCH_WHAT_YOU_TYPED) {
        exact_search_result = std::move(result);
      } else {
        std::string key = result.destination_url.spec();
        if (key.empty()) {
          key = NormalizeMatchText(result.title);
        }
        if (search_suggestion_keys.insert(key).second) {
          search_suggestion_results.push_back(
              RankedResult{std::move(result), match.relevance});
        }
      }
      continue;
    }

    // Omnibox navigation matches expose a generic globe until the favicon
    // database answers. Paint the same deterministic site chip used by
    // history and typed-domain results immediately, then upgrade it in
    // OnFaviconLoaded().
    result.icon_model = GetFaviconModelForURL(result.destination_url);
    result.title =
        match.description.empty() ? match.contents : match.description;
    result.subtitle = match.contents;
    result.badge = u"Go  ›";
    if (const Result* open_tab = find_open_tab(result.destination_url)) {
      result.title = open_tab->title.empty() ? result.title : open_tab->title;
      result.subtitle = open_tab->subtitle;
      result.space_id = open_tab->space_id;
      result.switch_to_tab = true;
      result.icon_model = open_tab->icon_model;
    } else {
      RequestFavicon(result.destination_url, /*allow_network_fetch=*/true);
    }

    if (result.title.empty()) {
      result.title = match.fill_into_edit;
    }
    const int match_score = std::max(
        ScoreSiteMatch(normalized_input, result.title, result.destination_url),
        3500);
    add_navigation_result(std::move(result), match_score + match.relevance);
  }

  // Browser destinations are useful even in a clean profile where history
  // cannot yet provide a match. Keep them in the same ranked Top Hits shelf
  // as open pages, history, and omnibox navigation suggestions.
  const int extension_query_score = std::max(
      {ScoreCandidateText(normalized_input, "chrome extensions"),
       ScoreCandidateText(normalized_input, "browser extensions"),
       ScoreCandidateText(normalized_input, "extension store")});
  if (extension_query_score >= 0) {
    Result web_store_result;
    web_store_result.title = u"Chrome Web Store — Extensions";
    web_store_result.subtitle = u"chromewebstore.google.com";
    web_store_result.badge = u"Open  ›";
    web_store_result.destination_url =
        extension_urls::GetWebstoreExtensionsCategoryURL();
    web_store_result.icon_model =
        GetFaviconModelForURL(web_store_result.destination_url);
    RequestFavicon(web_store_result.destination_url,
                   /*allow_network_fetch=*/true);
    add_navigation_result(std::move(web_store_result),
                          extension_query_score + 8000);

    Result manage_extensions_result;
    manage_extensions_result.title = u"Manage extensions";
    manage_extensions_result.subtitle = u"Brave settings";
    manage_extensions_result.badge = u"Open  ›";
    manage_extensions_result.destination_url = GURL(
        std::string(content::kBraveUIScheme) + "://extensions/");
    manage_extensions_result.icon = &vector_icons::kChromeExtensionIcon;
    add_navigation_result(std::move(manage_extensions_result),
                          extension_query_score + 7000);
  }

  if (normalized_input.size() >= 2) {
    for (const OriginPopularSite& site : kOriginPopularSites) {
      const GURL destination_url(site.url);
      const int match_score = std::max(
          ScoreCandidateText(normalized_input, site.keyword),
          ScoreSiteMatch(normalized_input, base::UTF8ToUTF16(site.title),
                         destination_url));
      if (match_score < 0) {
        continue;
      }

      Result result;
      result.title = base::UTF8ToUTF16(NormalizeHost(destination_url));
      result.badge = u"Open URL";
      result.destination_url = destination_url;
      result.icon_model = GetFaviconModelForURL(destination_url);
      if (const Result* open_tab = find_open_tab(destination_url)) {
        result = *open_tab;
        result.badge = u"Switch  ›";
      } else {
        RequestFavicon(destination_url, /*allow_network_fetch=*/true);
      }
      add_navigation_result(std::move(result), match_score + 6000);
    }
  }

  if (std::optional<GURL> typed_domain =
          BuildTypedDomainCandidate(trimmed_input)) {
    Result result;
    result.title = base::UTF8ToUTF16(NormalizeHost(*typed_domain));
    result.badge = u"Open URL";
    result.destination_url = *typed_domain;
    result.icon_model = GetFaviconModelForURL(*typed_domain);
    if (const Result* open_tab = find_open_tab(*typed_domain)) {
      result = *open_tab;
      result.badge = u"Switch  ›";
    } else {
      RequestFavicon(*typed_domain, /*allow_network_fetch=*/true);
    }
    add_navigation_result(std::move(result), 16000);
  }

  std::stable_sort(
      navigation_results.begin(), navigation_results.end(),
      [](const RankedResult& left, const RankedResult& right) {
        if (left.result.switch_to_tab != right.result.switch_to_tab) {
          return left.result.switch_to_tab;
        }
        if (left.score != right.score) {
          return left.score > right.score;
        }
        return left.result.title < right.result.title;
      });
  std::stable_sort(
      search_suggestion_results.begin(), search_suggestion_results.end(),
      [](const RankedResult& left, const RankedResult& right) {
        return left.score > right.score;
      });

  Result search_result;
  if (exact_search_result) {
    search_result = std::move(*exact_search_result);
  } else {
    search_result.title = trimmed_input;
    search_result.subtitle = u"— on Brave Search";
    search_result.badge = u"Search";
    search_result.is_search = true;
    search_result.icon = &vector_icons::kSearchIcon;
  }
  query_results_[0].push_back(std::move(search_result));

  bool prefer_top_hit = false;
  std::optional<Result> open_as_new_result;
  if (!navigation_results.empty()) {
    const Result& best_result = navigation_results.front().result;
    const bool host_prefix =
        normalized_input.find_first_of(" \t\r\n/:?#@") == std::string::npos &&
        NormalizeHost(best_result.destination_url).starts_with(
            normalized_input);
    prefer_top_hit = best_result.switch_to_tab || host_prefix;
    if (best_result.switch_to_tab) {
      Result open_as_new = best_result;
      open_as_new.switch_to_tab = false;
      open_as_new.space_id.clear();
      open_as_new.title =
          base::UTF8ToUTF16(NormalizeHost(open_as_new.destination_url));
      open_as_new.subtitle.clear();
      open_as_new.badge = u"Open URL";
      open_as_new_result = std::move(open_as_new);
      navigation_results.front().result.badge = u"Switch to page  ↵";
    }
  }

  while (query_results_[1].size() < kSecondarySectionCapacity &&
         !navigation_results.empty()) {
    query_results_[1].push_back(
        std::move(navigation_results.front().result));
    navigation_results.erase(navigation_results.begin());
  }
  while (query_results_[1].size() < kSecondarySectionCapacity &&
         !search_suggestion_results.empty()) {
    query_results_[1].push_back(
        std::move(search_suggestion_results.front().result));
    search_suggestion_results.erase(search_suggestion_results.begin());
  }

  if (open_as_new_result) {
    query_results_[2].push_back(std::move(*open_as_new_result));
  }
  while (query_results_[2].size() < kTertiarySectionCapacity &&
         !navigation_results.empty()) {
    query_results_[2].push_back(
        std::move(navigation_results.front().result));
    navigation_results.erase(navigation_results.begin());
  }
  while (query_results_[2].size() < kTertiarySectionCapacity &&
         !search_suggestion_results.empty()) {
    query_results_[2].push_back(
        std::move(search_suggestion_results.front().result));
    search_suggestion_results.erase(search_suggestion_results.begin());
  }

  // Keep exact/direct destinations and already-open pages as Enter's default
  // without moving the Search action out of its stable first visual section.
  selected_result_ = prefer_top_hit && !query_results_[1].empty() ? 1 : 0;
  UpdateResultRows();
  ApplyInlineAutocomplete();
}

void OriginQuickOpenView::ApplyInlineAutocomplete() {
  const gfx::Range selected_range = search_field_->GetSelectedRange();
  const bool showing_inline_completion =
      !selected_range.is_empty() &&
      selected_range.GetMin() == user_input_.size() &&
      selected_range.GetMax() == search_field_->GetText().size();

  std::u16string completion;
  const std::string typed = base::ToLowerASCII(base::UTF16ToUTF8(user_input_));
  const Result* completion_result = nullptr;
  for (size_t section : {1u, 2u}) {
    const auto navigation_result = std::ranges::find_if(
        query_results_[section],
        [](const Result& result) { return !result.is_search; });
    if (navigation_result != query_results_[section].end()) {
      completion_result = &*navigation_result;
      break;
    }
  }
  if (!suppress_inline_autocomplete_ && typed.size() >= 2 &&
      typed.find_first_of(" \t\r\n/:?#@") == std::string::npos &&
      completion_result) {
    const std::string host =
        NormalizeHost(completion_result->destination_url);
    if (host.size() > typed.size() && host.starts_with(typed)) {
      completion = user_input_ + base::UTF8ToUTF16(host.substr(typed.size()));
    }
  }

  if (completion.empty()) {
    if (showing_inline_completion) {
      updating_inline_autocomplete_ = true;
      search_field_->SetText(user_input_);
      search_field_->SetSelectedRange(gfx::Range(user_input_.size()));
      updating_inline_autocomplete_ = false;
    }
    return;
  }

  if (!showing_inline_completion && search_field_->GetText() != user_input_) {
    return;
  }
  updating_inline_autocomplete_ = true;
  search_field_->SetText(completion);
  search_field_->SetSelectedRange(
      gfx::Range(completion.size(), user_input_.size()));
  updating_inline_autocomplete_ = false;
}

void OriginQuickOpenView::LoadZeroStateData() {
  if (HasQuery()) {
    return;
  }

  task_tracker_.TryCancelAll();
  zero_state_results_ = {};
  open_tab_results_.clear();
  history_results_.clear();
  query_history_results_.clear();

  WorkspaceService* workspace_service =
      WorkspaceServiceFactory::GetForProfile(profile_);
  OriginSpaceController* current_space_controller =
      browser_->GetFeatures().origin_space_controller();
  const std::string active_space_id =
      current_space_controller ? current_space_controller->active_space_id()
                               : std::string();
  std::map<std::string, base::Time> space_last_active;
  if (workspace_service) {
    for (const OriginSpaceMetadata& space :
         workspace_service->GetOriginSpaces()) {
      space_last_active.emplace(space.id, base::Time());
    }
  }

  ProfileBrowserCollection::GetForProfile(profile_)->ForEach(
      [&](BrowserWindowInterface* browser_window) {
        TabStripModel* tab_strip_model = browser_window->GetTabStripModel();
        if (!tab_strip_model) {
          return true;
        }
        OriginSpaceController* space_controller =
            browser_window->GetFeatures().origin_space_controller();
        for (int index = 0; index < tab_strip_model->count(); ++index) {
          tabs::TabInterface* tab = tab_strip_model->GetTabAtIndex(index);
          if (!tab || !IsUsefulPageURL(tab->GetURL())) {
            continue;
          }

          content::WebContents* contents = tab->GetContents();
          const std::string space_id =
              space_controller && contents
                  ? space_controller->GetSpaceIdForTab(contents)
                  : std::string();
          const base::Time last_active = tab->GetLastActiveTime();
          if (!space_id.empty()) {
            auto found = space_last_active.find(space_id);
            if (found != space_last_active.end() &&
                found->second < last_active) {
              found->second = last_active;
            }
          }

          Result result;
          result.destination_url = tab->GetURL();
          result.space_id = space_id;
          result.switch_to_tab = true;
          result.badge = u"Switch  ›";
          if (TabUIHelper* tab_helper = TabUIHelper::From(tab)) {
            result.title = tab_helper->GetTitle();
            result.icon_model = tab_helper->GetFavicon();
          } else {
            result.title = tab->GetTitle();
          }
          if (result.title.empty()) {
            result.title = base::UTF8ToUTF16(result.destination_url.host());
          }
          if (result.icon_model.IsEmpty()) {
            result.icon_model = GetFaviconModelForURL(result.destination_url);
          }
          RequestFavicon(result.destination_url);

          const OriginSpaceMetadata* space =
              workspace_service && !space_id.empty()
                  ? workspace_service->GetOriginSpace(space_id)
                  : nullptr;
          if (browser_window == browser_ && space_id == active_space_id) {
            result.subtitle = u"— already open here";
          } else if (space) {
            result.subtitle =
                u"— already open in " + base::UTF8ToUTF16(space->name);
          } else {
            result.subtitle = u"— already open";
          }
          open_tab_results_.push_back(
              TimedResult{std::move(result), last_active});
        }
        return true;
      },
      BrowserCollection::Order::kActivation);

  std::stable_sort(open_tab_results_.begin(), open_tab_results_.end(),
                   [](const TimedResult& left, const TimedResult& right) {
                     return left.last_active > right.last_active;
                   });
  std::set<std::string> seen_urls;
  for (const TimedResult& open_tab : open_tab_results_) {
    if (!seen_urls.insert(open_tab.result.destination_url.spec()).second) {
      continue;
    }
    zero_state_results_[0].push_back(open_tab.result);
    if (zero_state_results_[0].size() == kPrimarySectionCapacity) {
      break;
    }
  }

  if (workspace_service) {
    std::vector<TimedResult> recent_spaces;
    for (const OriginSpaceMetadata& space :
         workspace_service->GetOriginSpaces()) {
      Result result;
      result.title = base::UTF8ToUTF16(space.name);
      result.icon = &GetOriginQuickOpenSpaceIcon(space.icon);
      result.space_id = space.id;
      const base::Time last_active = space_last_active[space.id];
      const std::u16string elapsed = FormatElapsedTime(last_active);
      result.subtitle = space.id == active_space_id
                            ? u"— Current Space"
                            : (elapsed.empty() ? u"— Space" : u"— " + elapsed);
      recent_spaces.push_back(TimedResult{std::move(result), last_active});
    }
    std::stable_sort(recent_spaces.begin(), recent_spaces.end(),
                     [](const TimedResult& left, const TimedResult& right) {
                       return left.last_active > right.last_active;
                     });
    for (TimedResult& space : recent_spaces) {
      zero_state_results_[1].push_back(std::move(space.result));
      if (zero_state_results_[1].size() == kSecondarySectionCapacity) {
        break;
      }
    }
  }

  selected_result_ = 0;
  UpdateResultRows();
  QueryRecentHistory();
}

void OriginQuickOpenView::QueryRecentHistory() {
  QueryHistoryForInput(std::u16string());
}

void OriginQuickOpenView::QueryHistoryForInput(const std::u16string& input) {
  history::HistoryService* history_service =
      HistoryServiceFactory::GetForProfile(profile_,
                                           ServiceAccessType::EXPLICIT_ACCESS);
  if (!history_service) {
    return;
  }

  std::u16string trimmed_input;
  base::TrimWhitespace(input, base::TrimPositions::TRIM_ALL, &trimmed_input);
  task_tracker_.TryCancelAll();
  if (!trimmed_input.empty()) {
    query_history_results_.clear();
  }

  history::QueryOptions options;
  if (trimmed_input.empty()) {
    options.SetRecentDayRange(90);
  }
  options.max_count = static_cast<int>(trimmed_input.empty()
                                           ? kMaxHistoryResults
                                           : kMaxQueryHistoryResults);
  options.duplicate_policy = history::QueryOptions::REMOVE_ALL_DUPLICATES;
  options.visit_order = history::QueryOptions::RECENT_FIRST;
  history_service->QueryHistory(
      trimmed_input, options,
      base::BindOnce(&OriginQuickOpenView::OnHistoryQueryComplete,
                     weak_factory_.GetWeakPtr(), trimmed_input),
      &task_tracker_);
}

void OriginQuickOpenView::OnHistoryQueryComplete(
    std::u16string requested_input,
    history::QueryResults results) {
  if (NormalizeMatchText(requested_input) !=
      NormalizeMatchText(user_input_)) {
    return;
  }

  const bool is_query_history = !NormalizeMatchText(requested_input).empty();
  std::vector<TimedResult>& destination_results =
      is_query_history ? query_history_results_ : history_results_;
  destination_results.clear();
  for (const history::URLResult& history_result : results) {
    if (!IsUsefulPageURL(history_result.url()) || history_result.hidden()) {
      continue;
    }
    Result result;
    result.title = history_result.title().empty()
                       ? base::UTF8ToUTF16(history_result.url().host())
                       : history_result.title();
    result.destination_url = history_result.url();
    result.subtitle = u"— " + FormatElapsedTime(history_result.last_visit());
    result.badge = u"Go  ›";
    result.icon_model = GetFaviconModelForURL(result.destination_url);
    destination_results.push_back(
        TimedResult{std::move(result), history_result.last_visit()});
  }

  if (is_query_history || HasQuery()) {
    RebuildResults();
    return;
  }

  std::set<std::string> top_hit_urls;
  for (const Result& result : zero_state_results_[0]) {
    top_hit_urls.insert(result.destination_url.spec());
  }

  std::vector<const history::URLResult*> ranked_history;
  for (const history::URLResult& history_result : results) {
    if (IsUsefulPageURL(history_result.url()) && !history_result.hidden()) {
      ranked_history.push_back(&history_result);
    }
  }
  std::stable_sort(
      ranked_history.begin(), ranked_history.end(),
      [](const history::URLResult* left, const history::URLResult* right) {
        const int left_score = left->typed_count() * 8 + left->visit_count();
        const int right_score = right->typed_count() * 8 + right->visit_count();
        return left_score == right_score
                   ? left->last_visit() > right->last_visit()
                   : left_score > right_score;
      });

  for (const history::URLResult* history_result : ranked_history) {
    if (zero_state_results_[0].size() == kPrimarySectionCapacity) {
      break;
    }
    if (!top_hit_urls.insert(history_result->url().spec()).second) {
      continue;
    }
    Result result;
    result.title = history_result->title().empty()
                       ? base::UTF8ToUTF16(history_result->url().host())
                       : history_result->title();
    result.destination_url = history_result->url();
    result.badge = u"Go  ›";
    result.icon_model = GetFaviconModelForURL(result.destination_url);
    const auto open_tab = std::ranges::find_if(
        open_tab_results_, [&](const TimedResult& candidate) {
          return candidate.result.destination_url == result.destination_url;
        });
    if (open_tab != open_tab_results_.end()) {
      result.subtitle = open_tab->result.subtitle;
      result.space_id = open_tab->result.space_id;
      result.switch_to_tab = true;
      result.icon_model = open_tab->result.icon_model;
    }
    RequestFavicon(result.destination_url);
    zero_state_results_[0].push_back(std::move(result));
  }

  zero_state_results_[2].clear();
  for (const history::URLResult& history_result : results) {
    if (zero_state_results_[2].size() == kTertiarySectionCapacity) {
      break;
    }
    if (!IsUsefulPageURL(history_result.url()) || history_result.hidden() ||
        top_hit_urls.contains(history_result.url().spec())) {
      continue;
    }

    Result result;
    result.title = history_result.title().empty()
                       ? base::UTF8ToUTF16(history_result.url().host())
                       : history_result.title();
    result.destination_url = history_result.url();
    result.badge = u"Go  ›";
    result.icon_model = GetFaviconModelForURL(result.destination_url);
    result.subtitle = u"— " + FormatElapsedTime(history_result.last_visit());
    const auto open_tab = std::ranges::find_if(
        open_tab_results_, [&](const TimedResult& candidate) {
          return candidate.result.destination_url == result.destination_url;
        });
    if (open_tab != open_tab_results_.end()) {
      result.switch_to_tab = true;
      result.space_id = open_tab->result.space_id;
      result.icon_model = open_tab->result.icon_model;
      WorkspaceService* workspace_service =
          WorkspaceServiceFactory::GetForProfile(profile_);
      const OriginSpaceMetadata* space =
          workspace_service && !result.space_id.empty()
              ? workspace_service->GetOriginSpace(result.space_id)
              : nullptr;
      if (space) {
        result.subtitle.append(u" in ").append(base::UTF8ToUTF16(space->name));
      }
    }
    RequestFavicon(result.destination_url);
    zero_state_results_[2].push_back(std::move(result));
  }

  UpdateResultRows();
}

ui::ImageModel OriginQuickOpenView::GetFaviconModelForURL(
    const GURL& url) const {
  const auto favicon = favicon_models_.find(NormalizeHost(url));
  return favicon == favicon_models_.end()
             ? GetOriginFallbackFaviconModel(url)
             : favicon->second;
}

void OriginQuickOpenView::RequestFavicon(const GURL& url,
                                         bool allow_network_fetch) {
  const std::string host = NormalizeHost(url);
  if (host.empty() || favicon_models_.contains(host) || !IsUsefulPageURL(url)) {
    return;
  }
  if (allow_network_fetch) {
    network_favicon_hosts_.insert(host);
  }
  if (!pending_favicon_hosts_.insert(host).second) {
    return;
  }
  favicon::FaviconService* favicon_service =
      FaviconServiceFactory::GetForProfile(profile_,
                                           ServiceAccessType::EXPLICIT_ACCESS);
  if (!favicon_service) {
    pending_favicon_hosts_.erase(host);
    network_favicon_hosts_.erase(host);
    return;
  }
  // Search results often identify a host before an exact page has been
  // visited. Use the largest cached icon across all supported site-icon types
  // and permit a host match, then downsample it to the 16 px UI slot.
  favicon_service->GetRawFaviconForPageURL(
      url,
      {favicon_base::IconType::kFavicon, favicon_base::IconType::kTouchIcon,
       favicon_base::IconType::kTouchPrecomposedIcon,
       favicon_base::IconType::kWebManifestIcon},
      /*desired_size_in_pixel=*/0, /*fallback_to_host=*/true,
      base::BindOnce(&OriginQuickOpenView::OnFaviconLoaded,
                     weak_factory_.GetWeakPtr(), url),
      &favicon_task_tracker_);
}

void OriginQuickOpenView::OnFaviconLoaded(
    const GURL& url,
    const favicon_base::FaviconRawBitmapResult& bitmap_result) {
  const std::string host = NormalizeHost(url);
  if (bitmap_result.is_valid()) {
    const gfx::Image favicon =
        gfx::Image::CreateFrom1xPNGBytes(bitmap_result.bitmap_data);
    if (!favicon.IsEmpty()) {
      pending_favicon_hosts_.erase(host);
      network_favicon_hosts_.erase(host);
      ApplyFavicon(url, favicon);
      return;
    }
  }

  if (network_favicon_hosts_.contains(host)) {
    RequestFaviconFromNetwork(url);
    return;
  }

  pending_favicon_hosts_.erase(host);
}

void OriginQuickOpenView::RequestFaviconFromNetwork(
    const GURL& url,
    bool allow_known_site_override) {
  const std::string host = NormalizeHost(url);
  image_fetcher::ImageFetcherService* service =
      ImageFetcherServiceFactory::GetForKey(profile_->GetProfileKey());
  image_fetcher::ImageFetcher* fetcher =
      service ? service->GetImageFetcher(
                    image_fetcher::ImageFetcherConfig::kDiskCacheOnly)
              : nullptr;
  if (!fetcher) {
    pending_favicon_hosts_.erase(host);
    network_favicon_hosts_.erase(host);
    return;
  }

  image_fetcher::ImageFetcherParams params(kQuickOpenFaviconTrafficAnnotation,
                                           kImageFetcherUmaClientName);
  params.set_max_download_size(kMaxFaviconDownloadBytes);
  params.set_frame_size(gfx::Size(32, 32));
  const std::optional<GURL> known_site_favicon =
      allow_known_site_override ? GetOriginKnownSiteFaviconURL(url)
                                : std::nullopt;
  fetcher->FetchImage(
      known_site_favicon.value_or(TemplateURL::GenerateFaviconURL(url)),
      base::BindOnce(&OriginQuickOpenView::OnNetworkFaviconLoaded,
                     weak_factory_.GetWeakPtr(), url,
                     known_site_favicon.has_value()),
      std::move(params));
}

void OriginQuickOpenView::OnNetworkFaviconLoaded(
    const GURL& url,
    bool used_known_site_override,
    const gfx::Image& image,
    const image_fetcher::RequestMetadata& request_metadata) {
  if (image.IsEmpty() && used_known_site_override) {
    RequestFaviconFromNetwork(url, /*allow_known_site_override=*/false);
    return;
  }

  const std::string host = NormalizeHost(url);
  pending_favicon_hosts_.erase(host);
  network_favicon_hosts_.erase(host);
  if (!image.IsEmpty()) {
    ApplyFavicon(url, image);
  }
}

void OriginQuickOpenView::ApplyFavicon(const GURL& url,
                                       const gfx::Image& favicon) {
  const std::string host = NormalizeHost(url);
  gfx::ImageSkia resized = gfx::ImageSkiaOperations::CreateResizedImage(
      favicon.AsImageSkia(), skia::ImageOperations::RESIZE_BEST,
      gfx::Size(20, 20));
  const ui::ImageModel image_model = ui::ImageModel::FromImageSkia(resized);
  favicon_models_[host] = image_model;
  const auto apply_image = [&](auto& sections) {
    for (auto& section : sections) {
      for (Result& result : section) {
        if (NormalizeHost(result.destination_url) == host) {
          result.icon_model = image_model;
        }
      }
    }
  };
  apply_image(zero_state_results_);
  apply_image(query_results_);
  for (TimedResult& result : history_results_) {
    if (NormalizeHost(result.result.destination_url) == host) {
      result.result.icon_model = image_model;
    }
  }
  for (TimedResult& result : query_history_results_) {
    if (NormalizeHost(result.result.destination_url) == host) {
      result.result.icon_model = image_model;
    }
  }
  for (TimedResult& result : open_tab_results_) {
    if (NormalizeHost(result.result.destination_url) == host) {
      result.result.icon_model = image_model;
    }
  }
  UpdateResultRows();
}

void OriginQuickOpenView::UpdateResultRows() {
  const bool has_query = HasQuery();
  displayed_results_ = has_query ? query_results_ : zero_state_results_;
  const std::array<std::u16string, kSectionCount> labels =
      has_query ? std::array<std::u16string, kSectionCount>{
                      u"Search", u"Top Hits", u"More Results"}
                : std::array<std::u16string, kSectionCount>{
                      u"Top Hits", u"Recent Workspaces", u"Recent Pages"};

  visible_results_.clear();
  for (size_t section = 0; section < kSectionCount; ++section) {
    const bool searching =
        has_query && section == 0 && displayed_results_[0].empty() &&
        displayed_results_[1].empty() && displayed_results_[2].empty();
    section_labels_[section]->SetText(searching ? u"Searching"
                                                : labels[section]);
    section_labels_[section]->SetVisible(!displayed_results_[section].empty() ||
                                         searching || !has_query);
    for (size_t index = 0; index < section_rows_[section].size(); ++index) {
      OriginQuickOpenResultButton* row = section_rows_[section][index];
      const bool visible = index < displayed_results_[section].size();
      row->SetVisible(visible);
      if (!visible) {
        continue;
      }
      const Result& result = displayed_results_[section][index];
      row->SetResult(result.title, result.subtitle, result.badge,
                     result.icon_model, result.icon,
                     /*persistent_badge=*/false);
      visible_results_.emplace_back(section, index);
    }
  }

  const bool zero_state_is_empty =
      !has_query && zero_state_results_[0].empty() &&
      zero_state_results_[1].empty() && zero_state_results_[2].empty();
  empty_state_label_->SetText(u"Your frequently used pages will appear here.");
  empty_state_label_->SetVisible(zero_state_is_empty);
  command_row_->SetVisible(false);
  command_row_->SetPaletteSelected(false);
  const bool show_space_actions = has_query && !send_to_space_ids_.empty();
  send_to_space_label_->SetVisible(show_space_actions);
  send_to_space_container_->SetVisible(show_space_actions);
  shortcuts_footer_->SetVisible(has_query);

  if (visible_results_.empty()) {
    selected_result_ = 0;
  } else {
    selected_result_ = std::min(selected_result_, visible_results_.size() - 1);
  }
  for (size_t index = 0; index < visible_results_.size(); ++index) {
    const auto [section, row_index] = visible_results_[index];
    section_rows_[section][row_index]->SetPaletteSelected(index ==
                                                          selected_result_);
  }
  UpdateSearchIcon();
  InvalidateLayout();
}

void OriginQuickOpenView::UpdateSearchIcon() {
  // Result favicons belong to the result rows. The query affordance remains a
  // stable search glyph while inline completion changes underneath it.
  const auto colors = GetOriginQuickOpenColors(GetColorProvider());
  search_icon_->SetImage(ui::ImageModel::FromVectorIcon(
      vector_icons::kSearchIcon, colors.secondary_text, 18));
}

void OriginQuickOpenView::RebuildSpaceControls() {
  WorkspaceService* workspace_service =
      WorkspaceServiceFactory::GetForProfile(profile_);
  OriginSpaceController* space_controller =
      browser_->GetFeatures().origin_space_controller();
  if (!workspace_service || !space_controller || !current_space_chip_ ||
      !send_to_space_container_) {
    return;
  }

  const auto& spaces = workspace_service->GetOriginSpaces();
  const size_t active_index = [&]() {
    const auto active = std::ranges::find(
        spaces, space_controller->active_space_id(), &OriginSpaceMetadata::id);
    return active == spaces.end()
               ? 0u
               : static_cast<size_t>(active - spaces.begin());
  }();
  if (!spaces.empty()) {
    current_space_chip_->SetSpace(spaces[active_index],
                                  GetSpaceAccentColor(active_index),
                                  std::u16string());
  }

  send_to_space_chips_.clear();
  send_to_space_ids_.clear();
  send_to_space_container_->RemoveAllChildViews();
  const size_t visible_space_count = std::min<size_t>(5, spaces.size());
  for (size_t index = 0; index < visible_space_count; ++index) {
    auto* chip = send_to_space_container_->AddChildView(
        std::make_unique<OriginQuickOpenSpaceChip>(
            base::BindRepeating(&OriginQuickOpenView::SubmitToSpace,
                                base::Unretained(this), index),
            /*current_space=*/false));
    chip->SetSpace(spaces[index], GetSpaceAccentColor(index),
                   base::NumberToString16(index + 1));
    send_to_space_chips_.push_back(chip);
    send_to_space_ids_.push_back(spaces[index].id);
  }
  send_to_space_container_->InvalidateLayout();
}

void OriginQuickOpenView::SelectResult(size_t index) {
  if (index >= visible_results_.size()) {
    return;
  }
  selected_result_ = index;
  for (size_t result_index = 0; result_index < visible_results_.size();
       ++result_index) {
    const auto [section, row_index] = visible_results_[result_index];
    section_rows_[section][row_index]->SetPaletteSelected(result_index ==
                                                          selected_result_);
  }
  UpdateSearchIcon();
}

bool OriginQuickOpenView::HasQuery() const {
  std::u16string trimmed;
  base::TrimWhitespace(user_input_, base::TrimPositions::TRIM_ALL, &trimmed);
  return !trimmed.empty();
}
