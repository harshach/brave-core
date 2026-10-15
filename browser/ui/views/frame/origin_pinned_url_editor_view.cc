// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/views/frame/origin_pinned_url_editor_view.h"

#include <algorithm>
#include <memory>
#include <string_view>
#include <utility>

#include "base/strings/escape.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "brave/browser/ui/views/frame/origin_chrome_style.h"
#include "components/url_formatter/url_fixer.h"
#include "components/url_formatter/url_formatter.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/ime/text_input_type.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"

namespace {

constexpr int kPanelWidth = 420;
constexpr int kPanelCornerRadius = 14;
constexpr int kFieldCornerRadius = 8;
// Drawn around the field only while the address can't be used.
constexpr int kFieldOutlineThickness = 1;

constexpr char16_t kHeading[] = u"Pinned address";
constexpr char16_t kPlaceholder[] = u"Address this page returns to";
constexpr char16_t kError[] = u"Enter a web address";

std::unique_ptr<views::Label> MakeLabel(std::u16string text,
                                        int size,
                                        gfx::Font::Weight weight) {
  auto label = std::make_unique<views::Label>(std::move(text));
  label->SetFontList(origin_chrome_style::Font(size, weight));
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  // The colors are picked for the card, not for the dialog background a
  // label otherwise assumes it sits on.
  label->SetAutoColorReadabilityEnabled(false);
  return label;
}

// Readable on the card in either theme.
SkColor ErrorColor(bool dark) {
  return dark ? SkColorSetRGB(0xFF, 0x7F, 0x76)
              : SkColorSetRGB(0xC8, 0x24, 0x1B);
}

// Whether `text` is just the character `key` types, in either case.
bool IsActivationCharacter(ui::KeyboardCode key, std::u16string_view text) {
  if (key < ui::VKEY_A || key > ui::VKEY_Z || text.size() != 1) {
    return false;
  }
  return base::ToLowerASCII(text[0]) == u'a' + (key - ui::VKEY_A);
}

// Only an address a pinned page can load to go home.
bool IsAcceptedHome(const GURL& url) {
  return url.is_valid() &&
         (url.SchemeIsHTTPOrHTTPS() ? url.has_host() : url.SchemeIsFile());
}

}  // namespace

OriginPinnedUrlEditorView::OriginPinnedUrlEditorView(
    base::RepeatingClosure on_close)
    : on_close_(std::move(on_close)) {
  SetVisible(false);
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);

  panel_ = AddChildView(std::make_unique<views::View>());
  panel_->SetPaintToLayer();
  panel_->layer()->SetFillsBoundsOpaquely(false);
  panel_->layer()->SetRoundedCornerRadius(
      gfx::RoundedCornersF(kPanelCornerRadius));
  panel_->layer()->SetBackgroundBlur(origin_chrome_style::kPickerBlurSigma);
  panel_->layer()->SetBackdropFilterQuality(
      origin_chrome_style::kPickerBackdropQuality);
  panel_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::TLBR(16, 18, 12, 18), 6));
  panel_->GetViewAccessibility().SetRole(ax::mojom::Role::kDialog);
  panel_->GetViewAccessibility().SetName(std::u16string(kHeading));

  heading_ = panel_->AddChildView(
      MakeLabel(kHeading, 15, gfx::Font::Weight::SEMIBOLD));

  page_title_ = panel_->AddChildView(
      MakeLabel(std::u16string(), 12, gfx::Font::Weight::NORMAL));
  page_title_->SetElideBehavior(gfx::ELIDE_TAIL);

  field_container_ = panel_->AddChildView(std::make_unique<views::View>());
  // With the outline, the same padding as the pickers' fields.
  field_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::VH(8 - kFieldOutlineThickness,
                      10 - kFieldOutlineThickness)));
  field_ = field_container_->AddChildView(std::make_unique<views::Textfield>());
  field_->SetPlaceholderText(kPlaceholder);
  field_->SetAccessibleName(kPlaceholder);
  field_->SetTextInputType(ui::TEXT_INPUT_TYPE_URL);
  field_->SetFontList(origin_chrome_style::Font(14, gfx::Font::Weight::NORMAL));
  field_->SetBackgroundEnabled(false);
  field_->SetBorder(views::CreateEmptyBorder(gfx::Insets()));
  field_->set_controller(this);

  error_ =
      panel_->AddChildView(MakeLabel(kError, 12, gfx::Font::Weight::NORMAL));
  error_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 2)));
  error_->SetVisible(false);

  footer_ = panel_->AddChildView(
      MakeLabel(u"Enter saves · Esc cancels", 11, gfx::Font::Weight::NORMAL));
  footer_->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(4, 2, 0, 0)));

  RefreshTheme();
}

OriginPinnedUrlEditorView::~OriginPinnedUrlEditorView() {
  field_->set_controller(nullptr);
}

void OriginPinnedUrlEditorView::Show(
    const GURL& home,
    const std::u16string& title,
    std::optional<ui::KeyboardCode> activation_key,
    SaveCallback on_save) {
  on_save_ = std::move(on_save);
  home_ = home;
  // In full, with the host readable where it's safe to show as Unicode.
  home_text_ = home.is_valid()
                   ? url_formatter::FormatUrl(
                         home, url_formatter::kFormatUrlOmitNothing,
                         base::UnescapeRule::NONE, nullptr, nullptr, nullptr)
                   : std::u16string();
  page_title_->SetText(title);
  page_title_->SetVisible(!title.empty());
  SetErrorVisible(false);
  field_->SetText(home_text_);
  activation_key_to_suppress_ = activation_key;
  SetVisible(true);
  InvalidateLayout();
  field_->RequestFocus();
  // Typing replaces the address; reversed keeps its start in view.
  field_->SelectAll(/*reversed=*/true);
}

void OriginPinnedUrlEditorView::Dismiss() {
  if (!GetVisible()) {
    return;
  }
  on_save_.Reset();
  activation_key_to_suppress_.reset();
  SetVisible(false);
  on_close_.Run();
}

void OriginPinnedUrlEditorView::Layout(PassKey) {
  const gfx::Rect available = GetContentsBounds();
  const int panel_width =
      std::min(kPanelWidth, std::max(0, available.width() - 48));
  const int panel_height = std::min(panel_->GetHeightForWidth(panel_width),
                                    std::max(0, available.height() - 24));
  // On the same shelf as Quick Open.
  const int free_height = std::max(0, available.height() - panel_height);
  const int panel_y = available.y() + std::clamp(free_height - 10, 12, 64);
  panel_->SetBounds(available.x() + (available.width() - panel_width) / 2,
                    panel_y, panel_width, panel_height);
}

void OriginPinnedUrlEditorView::OnThemeChanged() {
  views::View::OnThemeChanged();
  RefreshTheme();
}

bool OriginPinnedUrlEditorView::OnMousePressed(const ui::MouseEvent& event) {
  if (!panel_->bounds().Contains(event.location())) {
    Dismiss();
  }
  return true;
}

void OriginPinnedUrlEditorView::ContentsChanged(
    views::Textfield* sender,
    const std::u16string& new_contents) {
  // On some platforms the character of the key that opened the card arrives
  // after focus has moved here, replacing the selected address. Put it back.
  if (activation_key_to_suppress_) {
    const ui::KeyboardCode key = *activation_key_to_suppress_;
    activation_key_to_suppress_.reset();
    if (IsActivationCharacter(key, new_contents)) {
      field_->SetText(home_text_);
      field_->SelectAll(/*reversed=*/true);
      return;
    }
  }
  SetErrorVisible(false);
}

bool OriginPinnedUrlEditorView::HandleKeyEvent(views::Textfield* sender,
                                               const ui::KeyEvent& key_event) {
  if (activation_key_to_suppress_) {
    if (key_event.key_code() == *activation_key_to_suppress_) {
      // Stay armed until key-up, so a character that follows key-down is
      // still caught by ContentsChanged().
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
      Dismiss();
      return true;
    case ui::VKEY_RETURN:
      Submit();
      return true;
    default:
      return false;
  }
}

GURL OriginPinnedUrlEditorView::GetEnteredHome() const {
  const std::u16string_view text = field_->GetText();
  // Left as it opened, the address stays exactly what it was, whatever its
  // scheme.
  if (home_.is_valid() && text == home_text_) {
    return home_;
  }
  const GURL url =
      url_formatter::FixupURL(base::UTF16ToUTF8(text), std::string());
  return IsAcceptedHome(url) ? url : GURL();
}

bool OriginPinnedUrlEditorView::Submit() {
  if (!GetVisible() || !on_save_) {
    return false;
  }
  const GURL home = GetEnteredHome();
  if (!home.is_valid()) {
    SetErrorVisible(true);
    GetViewAccessibility().AnnounceAlert(kError);
    return false;
  }
  SaveCallback on_save = std::move(on_save_);
  const base::WeakPtr<OriginPinnedUrlEditorView> weak_this =
      weak_factory_.GetWeakPtr();
  std::move(on_save).Run(home);
  if (weak_this) {
    Dismiss();
  }
  return true;
}

void OriginPinnedUrlEditorView::SetErrorVisible(bool visible) {
  if (error_->GetVisible() == visible) {
    return;
  }
  error_->SetVisible(visible);
  RefreshTheme();
  InvalidateLayout();
}

void OriginPinnedUrlEditorView::RefreshTheme() {
  const bool dark = origin_chrome_style::IsDark(this);
  const origin_chrome_style::PickerColors colors =
      origin_chrome_style::GetPickerColors(dark);
  SetBackground(views::CreateSolidBackground(colors.scrim));
  panel_->SetBackground(
      views::CreateRoundedRectBackground(colors.panel, kPanelCornerRadius));
  panel_->SetBorder(views::CreateRoundedRectBorder(1, kPanelCornerRadius,
                                                   colors.panel_stroke));
  heading_->SetEnabledColor(colors.primary_text);
  page_title_->SetEnabledColor(colors.secondary_text);
  field_container_->SetBackground(
      views::CreateRoundedRectBackground(colors.field, kFieldCornerRadius));
  field_container_->SetBorder(views::CreateRoundedRectBorder(
      kFieldOutlineThickness, kFieldCornerRadius,
      error_->GetVisible() ? ErrorColor(dark) : SK_ColorTRANSPARENT));
  field_->SetColor(colors.primary_text);
  error_->SetEnabledColor(ErrorColor(dark));
  footer_->SetEnabledColor(colors.tertiary_text);
}

BEGIN_METADATA(OriginPinnedUrlEditorView)
END_METADATA
