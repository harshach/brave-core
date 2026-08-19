/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/views/frame/origin_quick_open_view.h"

#include <algorithm>
#include <memory>
#include <set>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "build/build_config.h"
#include "chrome/browser/autocomplete/chrome_autocomplete_provider_client.h"
#include "chrome/browser/profiles/profile.h"
#include "components/omnibox/browser/autocomplete_classifier.h"
#include "components/omnibox/browser/autocomplete_controller_config.h"
#include "components/omnibox/browser/autocomplete_enums.h"
#include "components/omnibox/browser/autocomplete_input.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/omnibox/browser/autocomplete_match_type.h"
#include "components/omnibox/browser/autocomplete_result.h"
#include "components/vector_icons/vector_icons.h"
#include "third_party/metrics_proto/omnibox_event.pb.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
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

constexpr int kPanelWidth = 780;
constexpr int kPanelCornerRadius = 18;
constexpr int kSearchCornerRadius = 12;
constexpr int kResultCornerRadius = 10;
constexpr int kResultRowHeight = 54;
constexpr size_t kMaxResults = 6;
constexpr float kPanelBlurSigma = 28.0f;
constexpr float kPanelBackdropQuality = 0.30f;

constexpr SkColor kScrimColor = SkColorSetARGB(90, 5, 6, 8);
constexpr SkColor kPanelColor = SkColorSetARGB(184, 22, 24, 27);
constexpr SkColor kPanelStrokeColor = SkColorSetARGB(220, 93, 96, 104);
constexpr SkColor kSearchColor = SkColorSetARGB(164, 31, 34, 38);
constexpr SkColor kSearchStrokeColor = SkColorSetARGB(215, 72, 75, 83);
constexpr SkColor kSelectedResultColor = SkColorSetARGB(230, 58, 32, 111);
constexpr SkColor kHoveredResultColor = SkColorSetARGB(172, 39, 42, 48);
constexpr SkColor kPurple = SkColorSetRGB(166, 112, 255);
constexpr SkColor kPurpleText = SkColorSetRGB(205, 177, 255);
constexpr SkColor kPrimaryText = SkColorSetRGB(246, 243, 249);
constexpr SkColor kSecondaryText = SkColorSetRGB(174, 170, 181);
constexpr SkColor kTertiaryText = SkColorSetRGB(129, 127, 138);

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

class OriginQuickOpenTextButton : public views::LabelButton {
  METADATA_HEADER(OriginQuickOpenTextButton, views::LabelButton)

 public:
  OriginQuickOpenTextButton(PressedCallback callback,
                            const std::u16string& text)
      : LabelButton(std::move(callback), text) {}

  void SetOriginFont(const gfx::FontList& font) { label()->SetFontList(font); }
};

BEGIN_METADATA(OriginQuickOpenTextButton)
END_METADATA

void StyleTextButton(OriginQuickOpenTextButton* button,
                     SkColor foreground,
                     SkColor background,
                     int radius) {
  button->SetHorizontalAlignment(gfx::HorizontalAlignment::ALIGN_CENTER);
  button->SetEnabledTextColors(foreground);
  button->SetOriginFont(OriginQuickOpenFont(13, gfx::Font::Weight::SEMIBOLD));
  button->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(8, 12)));
  button->SetBackground(views::CreateRoundedRectBackground(background, radius));
}

}  // namespace

// A two-line omnibox result row. The text field deliberately retains keyboard
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
    SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(7, 12)));

    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 12));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    icon_view_ = AddChildView(std::make_unique<views::ImageView>());
    icon_view_->SetPreferredSize(gfx::Size(20, 20));

    auto* labels = AddChildView(std::make_unique<views::View>());
    auto* labels_layout =
        labels->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kVertical, gfx::Insets(), 1));
    labels_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStart);
    layout->SetFlexForView(labels, 1);

    title_label_ = labels->AddChildView(std::make_unique<views::Label>());
    title_label_->SetHorizontalAlignment(gfx::HorizontalAlignment::ALIGN_LEFT);
    title_label_->SetElideBehavior(gfx::ELIDE_TAIL);
    title_label_->SetFontList(
        OriginQuickOpenFont(14, gfx::Font::Weight::MEDIUM));

    subtitle_label_ = labels->AddChildView(std::make_unique<views::Label>());
    subtitle_label_->SetHorizontalAlignment(
        gfx::HorizontalAlignment::ALIGN_LEFT);
    subtitle_label_->SetElideBehavior(gfx::ELIDE_MIDDLE);
    subtitle_label_->SetFontList(
        OriginQuickOpenFont(11, gfx::Font::Weight::NORMAL));

    badge_label_ = AddChildView(std::make_unique<views::Label>());
    badge_label_->SetFontList(
        OriginQuickOpenFont(11, gfx::Font::Weight::SEMIBOLD));
    badge_label_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(4, 8)));
    badge_label_->SetBackground(
        views::CreateRoundedRectBackground(SkColorSetRGB(44, 46, 53), 7));

    RefreshRowStyle();
  }

  void SetResult(const std::u16string& title,
                 const std::u16string& subtitle,
                 const std::u16string& badge,
                 const gfx::VectorIcon* icon) {
    title_label_->SetText(title);
    subtitle_label_->SetText(subtitle);
    subtitle_label_->SetVisible(!subtitle.empty());
    badge_label_->SetText(badge);
    badge_label_->SetVisible(!badge.empty());
    icon_ = icon;
    SetAccessibleName(subtitle.empty() ? title : title + u", " + subtitle);
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

 private:
  void RefreshRowStyle() {
    const bool hovered =
        GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    const SkColor background = palette_selected_ ? kSelectedResultColor
                               : hovered         ? kHoveredResultColor
                                                 : SK_ColorTRANSPARENT;
    SetBackground(
        views::CreateRoundedRectBackground(background, kResultCornerRadius));
    title_label_->SetEnabledColor(kPrimaryText);
    subtitle_label_->SetEnabledColor(palette_selected_ ? kPurpleText
                                                       : kSecondaryText);
    badge_label_->SetEnabledColor(palette_selected_ ? kPurpleText
                                                    : kSecondaryText);
    if (icon_) {
      icon_view_->SetImage(ui::ImageModel::FromVectorIcon(
          *icon_, palette_selected_ ? kPurpleText : kSecondaryText, 18));
    }
  }

  bool palette_selected_ = false;
  raw_ptr<const gfx::VectorIcon> icon_ = nullptr;
  raw_ptr<views::ImageView> icon_view_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::Label> subtitle_label_ = nullptr;
  raw_ptr<views::Label> badge_label_ = nullptr;
};

BEGIN_METADATA(OriginQuickOpenResultButton)
END_METADATA

BEGIN_METADATA(OriginQuickOpenView)
END_METADATA

OriginQuickOpenView::OriginQuickOpenView(Profile* profile,
                                         SubmitCallback submit_callback,
                                         base::RepeatingClosure close_callback)
    : submit_callback_(std::move(submit_callback)),
      close_callback_(std::move(close_callback)) {
  CHECK(profile);

  autocomplete_controller_ = std::make_unique<AutocompleteController>(
      std::make_unique<ChromeAutocompleteProviderClient>(profile),
      AutocompleteControllerConfig{
          .provider_types = AutocompleteClassifier::DefaultOmniboxProviders(),
          .unscoped_open_tab_suggestions = true});
  autocomplete_controller_->AddObserver(this);

  SetVisible(false);
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  SetBackground(views::CreateSolidBackground(kScrimColor));

  panel_ = AddChildView(std::make_unique<views::View>());
  panel_->SetPaintToLayer();
  panel_->layer()->SetFillsBoundsOpaquely(false);
  panel_->layer()->SetRoundedCornerRadius(
      gfx::RoundedCornersF(kPanelCornerRadius));
  panel_->layer()->SetBackgroundBlur(kPanelBlurSigma);
  panel_->layer()->SetBackdropFilterQuality(kPanelBackdropQuality);
  panel_->SetBackground(
      views::CreateRoundedRectBackground(kPanelColor, kPanelCornerRadius));
  panel_->SetBorder(
      views::CreateRoundedRectBorder(1, kPanelCornerRadius, kPanelStrokeColor));
  panel_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::TLBR(18, 18, 14, 18), 8));

  auto* search_row = panel_->AddChildView(std::make_unique<views::View>());
  search_row->SetPreferredSize(gfx::Size(0, 54));
  search_row->SetBackground(
      views::CreateRoundedRectBackground(kSearchColor, kSearchCornerRadius));
  search_row->SetBorder(views::CreateRoundedRectBorder(1, kSearchCornerRadius,
                                                       kSearchStrokeColor));
  auto* search_layout =
      search_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::TLBR(0, 14, 0, 8), 10));
  search_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  auto* search_icon =
      search_row->AddChildView(std::make_unique<views::ImageView>());
  search_icon->SetPreferredSize(gfx::Size(24, 24));
  search_icon->SetImage(ui::ImageModel::FromVectorIcon(
      vector_icons::kSearchIcon, kPrimaryText, 22));

  search_field_ =
      search_row->AddChildView(std::make_unique<views::Textfield>());
  search_field_->set_controller(this);
  search_field_->SetPlaceholderText(u"Search tabs, history, or the web…");
  search_field_->SetPlaceholderTextColorId(ui::kColorSysOnSurfaceSubtle);
  search_field_->SetTextColorId(ui::kColorSysOnSurface);
  search_field_->SetBackgroundEnabled(false);
  search_field_->SetBorder(views::CreateEmptyBorder(gfx::Insets()));
  search_field_->SetFontList(
      OriginQuickOpenFont(18, gfx::Font::Weight::NORMAL));
  search_layout->SetFlexForView(search_field_, 1);

  auto* replace_button =
      search_row->AddChildView(std::make_unique<OriginQuickOpenTextButton>(
          base::BindRepeating(&OriginQuickOpenView::Submit,
                              base::Unretained(this),
                              OriginQuickOpenDisposition::kReplace),
          u"⌥⏎  Replace"));
  StyleTextButton(replace_button, kSecondaryText, SK_ColorTRANSPARENT, 8);

  auto* split_button =
      search_row->AddChildView(std::make_unique<OriginQuickOpenTextButton>(
          base::BindRepeating(&OriginQuickOpenView::Submit,
                              base::Unretained(this),
                              OriginQuickOpenDisposition::kSplit),
          u"⇧⏎  Split Screen"));
  StyleTextButton(split_button, SkColorSetRGB(30, 17, 59), kPurple, 8);

  section_label_ =
      panel_->AddChildView(std::make_unique<views::Label>(u"QUICK OPEN"));
  section_label_->SetHorizontalAlignment(gfx::HorizontalAlignment::ALIGN_LEFT);
  section_label_->SetEnabledColor(kTertiaryText);
  section_label_->SetFontList(
      OriginQuickOpenFont(11, gfx::Font::Weight::SEMIBOLD));
  section_label_->SetBorder(
      views::CreateEmptyBorder(gfx::Insets::TLBR(5, 4, 1, 4)));

  for (size_t i = 0; i < kMaxResults; ++i) {
    auto* row = panel_->AddChildView(
        std::make_unique<OriginQuickOpenResultButton>(base::BindRepeating(
            &OriginQuickOpenView::OnResultPressed, base::Unretained(this), i)));
    row->SetVisible(false);
    result_rows_.push_back(row);
  }

  empty_state_label_ = panel_->AddChildView(std::make_unique<views::Label>(
      u"Type to search open tabs, browsing history, bookmarks, and the web."));
  empty_state_label_->SetHorizontalAlignment(
      gfx::HorizontalAlignment::ALIGN_LEFT);
  empty_state_label_->SetEnabledColor(kSecondaryText);
  empty_state_label_->SetFontList(
      OriginQuickOpenFont(13, gfx::Font::Weight::NORMAL));
  empty_state_label_->SetBorder(
      views::CreateEmptyBorder(gfx::Insets::TLBR(12, 12, 16, 12)));

  auto* divider = panel_->AddChildView(std::make_unique<views::View>());
  divider->SetPreferredSize(gfx::Size(0, 1));
  divider->SetBackground(
      views::CreateSolidBackground(SkColorSetRGB(48, 50, 56)));

  auto* keyboard_hint = panel_->AddChildView(std::make_unique<views::Label>(
      u"↑ ↓  choose    Enter  open    Shift+Enter  split    "
      u"Alt+Enter  replace    Esc  close"));
  keyboard_hint->SetHorizontalAlignment(gfx::HorizontalAlignment::ALIGN_RIGHT);
  keyboard_hint->SetEnabledColor(kTertiaryText);
  keyboard_hint->SetFontList(
      OriginQuickOpenFont(11, gfx::Font::Weight::NORMAL));
  keyboard_hint->SetBorder(
      views::CreateEmptyBorder(gfx::Insets::TLBR(3, 4, 0, 4)));

  UpdateResultRows();
}

OriginQuickOpenView::~OriginQuickOpenView() {
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
  autocomplete_controller_->Stop(AutocompleteStopReason::kClobbered);
  results_.clear();
  selected_result_ = 0;
  activation_key_to_suppress_ = activation_key;
  search_field_->SetText(std::u16string());
  UpdateResultRows();
  SetVisible(true);
  search_field_->RequestFocus();
}

void OriginQuickOpenView::Dismiss() {
  autocomplete_controller_->Stop(AutocompleteStopReason::kClobbered);
  activation_key_to_suppress_.reset();
  SetVisible(false);
}

void OriginQuickOpenView::ContentsChanged(views::Textfield* sender,
                                          const std::u16string& new_contents) {
  if (sender != search_field_) {
    return;
  }

  // On some platforms the character event belonging to the browser-level
  // shortcut arrives after focus has moved into this text field. Remove only
  // that initial activation character; subsequent typing remains untouched.
  if (activation_key_to_suppress_) {
    const bool is_open_shortcut_character =
        *activation_key_to_suppress_ == ui::VKEY_O &&
        (new_contents == u"o" || new_contents == u"O");
    activation_key_to_suppress_.reset();
    if (is_open_shortcut_character) {
      search_field_->SetText(std::u16string());
      results_.clear();
      selected_result_ = 0;
      UpdateResultRows();
      return;
    }
  }

  selected_result_ = 0;
  StartAutocomplete(new_contents);
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

  switch (key_event.key_code()) {
    case ui::VKEY_ESCAPE:
      close_callback_.Run();
      return true;
    case ui::VKEY_RETURN:
      if (key_event.IsShiftDown()) {
        Submit(OriginQuickOpenDisposition::kSplit);
      } else if (key_event.IsAltDown()) {
        Submit(OriginQuickOpenDisposition::kReplace);
      } else {
        Submit(OriginQuickOpenDisposition::kNewPage);
      }
      return true;
    case ui::VKEY_DOWN:
      if (!results_.empty()) {
        SelectResult((selected_result_ + 1) % results_.size());
      }
      return true;
    case ui::VKEY_UP:
      if (!results_.empty()) {
        SelectResult((selected_result_ + results_.size() - 1) %
                     results_.size());
      }
      return true;
    default:
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
      std::min(kPanelWidth, std::max(320, available.width() - 56));
  const int result_area_height =
      results_.empty() ? 72 : static_cast<int>(results_.size()) * 62;
  const int desired_height = 174 + result_area_height;
  const int panel_height =
      std::min(desired_height, std::max(230, available.height() - 56));
  panel_->SetBounds(available.x() + (available.width() - panel_width) / 2,
                    available.y() + (available.height() - panel_height) / 2,
                    panel_width, panel_height);
}

bool OriginQuickOpenView::OnMousePressed(const ui::MouseEvent& event) {
  if (!panel_->bounds().Contains(event.location())) {
    close_callback_.Run();
  }
  return true;
}

void OriginQuickOpenView::Submit(OriginQuickOpenDisposition disposition) {
  OriginQuickOpenSelection selection;
  selection.input = std::u16string(search_field_->GetText());
  if (!results_.empty() && selected_result_ < results_.size()) {
    selection.destination_url = results_[selected_result_].destination_url;
    selection.switch_to_tab = results_[selected_result_].switch_to_tab;
  }
  submit_callback_.Run(std::move(selection), disposition);
}

void OriginQuickOpenView::OnResultPressed(size_t index) {
  if (index >= results_.size()) {
    return;
  }
  SelectResult(index);
  Submit(OriginQuickOpenDisposition::kNewPage);
}

void OriginQuickOpenView::StartAutocomplete(const std::u16string& input) {
  std::u16string trimmed;
  base::TrimWhitespace(input, base::TrimPositions::TRIM_ALL, &trimmed);
  if (trimmed.empty()) {
    autocomplete_controller_->Stop(AutocompleteStopReason::kClobbered);
    results_.clear();
    UpdateResultRows();
    return;
  }

  AutocompleteInput autocomplete_input(
      input, input.size(), metrics::OmniboxEventProto::OTHER,
      autocomplete_controller_->autocomplete_provider_client()
          ->GetSchemeClassifier());
  autocomplete_input.set_prevent_inline_autocomplete(false);
  autocomplete_controller_->Start(autocomplete_input);
}

void OriginQuickOpenView::RebuildResults() {
  const AutocompleteResult& autocomplete_results =
      autocomplete_controller_->result();
  std::vector<const AutocompleteMatch*> ordered_matches;
  ordered_matches.reserve(autocomplete_results.size());

  // Switching to an already-open page is the least destructive action, so
  // surface those before otherwise-equivalent history and search matches.
  for (const AutocompleteMatch& match : autocomplete_results) {
    if (match.type == AutocompleteMatchType::OPEN_TAB) {
      ordered_matches.push_back(&match);
    }
  }
  for (const AutocompleteMatch& match : autocomplete_results) {
    if (match.type != AutocompleteMatchType::OPEN_TAB) {
      ordered_matches.push_back(&match);
    }
  }

  results_.clear();
  std::set<std::string> seen_urls;
  for (const AutocompleteMatch* match : ordered_matches) {
    if (!match->destination_url.is_valid()) {
      continue;
    }
    if (!seen_urls.insert(match->destination_url.spec()).second) {
      continue;
    }

    Result result;
    result.destination_url = match->destination_url;
    result.switch_to_tab = match->type == AutocompleteMatchType::OPEN_TAB;
    result.icon = &match->GetVectorIcon(/*is_bookmark=*/false);

    if (result.switch_to_tab) {
      result.title =
          match->description.empty() ? match->contents : match->description;
      result.subtitle = match->contents;
      result.badge = u"Switch";
    } else if (AutocompleteMatch::IsSearchType(match->type)) {
      result.title = match->contents;
      result.subtitle =
          match->description.empty() ? u"Search the web" : match->description;
      result.badge = u"Search";
    } else {
      result.title =
          match->description.empty() ? match->contents : match->description;
      result.subtitle = match->contents;
      result.badge = u"Open";
    }

    if (result.title.empty()) {
      result.title = match->fill_into_edit;
    }
    results_.push_back(std::move(result));
    if (results_.size() == kMaxResults) {
      break;
    }
  }

  selected_result_ =
      results_.empty() ? 0 : std::min(selected_result_, results_.size() - 1);
  UpdateResultRows();
}

void OriginQuickOpenView::UpdateResultRows() {
  const bool has_open_tab = std::ranges::any_of(
      results_, [](const Result& result) { return result.switch_to_tab; });
  if (results_.empty()) {
    std::u16string trimmed;
    base::TrimWhitespace(search_field_->GetText(),
                         base::TrimPositions::TRIM_ALL, &trimmed);
    section_label_->SetText(trimmed.empty() ? u"QUICK OPEN" : u"SEARCHING");
    empty_state_label_->SetText(
        trimmed.empty()
            ? u"Type to search open tabs, browsing history, bookmarks, and "
              u"the web."
            : u"Finding the closest pages and web results…");
  } else {
    section_label_->SetText(has_open_tab ? u"OPEN TABS & BEST MATCHES"
                                         : u"BEST MATCHES");
  }

  empty_state_label_->SetVisible(results_.empty());
  for (size_t i = 0; i < result_rows_.size(); ++i) {
    OriginQuickOpenResultButton* row = result_rows_[i];
    const bool visible = i < results_.size();
    row->SetVisible(visible);
    if (!visible) {
      continue;
    }
    const Result& result = results_[i];
    row->SetResult(result.title, result.subtitle, result.badge, result.icon);
    row->SetPaletteSelected(i == selected_result_);
  }
  InvalidateLayout();
}

void OriginQuickOpenView::SelectResult(size_t index) {
  if (index >= results_.size()) {
    return;
  }
  selected_result_ = index;
  for (size_t i = 0; i < result_rows_.size(); ++i) {
    result_rows_[i]->SetPaletteSelected(i == selected_result_);
  }
}
