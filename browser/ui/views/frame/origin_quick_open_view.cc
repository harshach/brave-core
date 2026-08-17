/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/views/frame/origin_quick_open_view.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "build/build_config.h"
#include "components/vector_icons/vector_icons.h"
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
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"

namespace {

constexpr int kPanelWidth = 760;
constexpr int kPanelHeight = 276;
constexpr int kPanelCornerRadius = 16;
constexpr SkColor kPanelColor = SkColorSetRGB(29, 31, 31);
constexpr SkColor kPanelStrokeColor = SkColorSetARGB(64, 255, 255, 255);
constexpr SkColor kSelectedResultColor = SkColorSetRGB(48, 25, 103);
constexpr SkColor kPurple = SkColorSetRGB(151, 103, 255);
constexpr SkColor kPrimaryText = SkColorSetRGB(239, 237, 242);
constexpr SkColor kSecondaryText = SkColorSetRGB(164, 161, 170);

class OriginQuickOpenButton : public views::LabelButton {
  METADATA_HEADER(OriginQuickOpenButton, views::LabelButton)

 public:
  OriginQuickOpenButton(PressedCallback callback, const std::u16string& text)
      : LabelButton(std::move(callback), text) {}

  void SetOriginFont(const gfx::FontList& font) { label()->SetFontList(font); }
};

BEGIN_METADATA(OriginQuickOpenButton)
END_METADATA

gfx::FontList OriginQuickOpenFont(int size, gfx::Font::Weight weight) {
#if BUILDFLAG(IS_MAC)
  constexpr char kFamily[] = "SF Pro Text";
#elif BUILDFLAG(IS_WIN)
  constexpr char kFamily[] = "Segoe UI";
#else
  constexpr char kFamily[] = "Inter";
#endif
  return gfx::FontList({kFamily}, gfx::Font::NORMAL, size, weight);
}

void StyleTextButton(OriginQuickOpenButton* button,
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

BEGIN_METADATA(OriginQuickOpenView)
END_METADATA

OriginQuickOpenView::OriginQuickOpenView(SubmitCallback submit_callback,
                                         base::RepeatingClosure close_callback)
    : submit_callback_(std::move(submit_callback)),
      close_callback_(std::move(close_callback)) {
  SetVisible(false);
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  SetBackground(views::CreateSolidBackground(SkColorSetARGB(154, 7, 8, 9)));

  panel_ = AddChildView(std::make_unique<views::View>());
  panel_->SetPaintToLayer();
  panel_->layer()->SetFillsBoundsOpaquely(false);
  panel_->layer()->SetRoundedCornerRadius(
      gfx::RoundedCornersF(kPanelCornerRadius));
  panel_->SetBackground(
      views::CreateRoundedRectBackground(kPanelColor, kPanelCornerRadius));
  panel_->SetBorder(
      views::CreateRoundedRectBorder(1, kPanelCornerRadius, kPanelStrokeColor));
  panel_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::TLBR(18, 20, 16, 20), 10));

  auto* search_row = panel_->AddChildView(std::make_unique<views::View>());
  search_row->SetPreferredSize(gfx::Size(0, 48));
  auto* search_layout =
      search_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 10));
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
  search_field_->SetPlaceholderText(u"Search or ask a question…");
  search_field_->SetPlaceholderTextColorId(ui::kColorSysOnSurfaceSubtle);
  search_field_->SetTextColorId(ui::kColorSysOnSurface);
  search_field_->SetBackgroundEnabled(false);
  search_field_->SetBorder(views::CreateEmptyBorder(gfx::Insets()));
  search_field_->SetFontList(
      OriginQuickOpenFont(18, gfx::Font::Weight::NORMAL));
  search_layout->SetFlexForView(search_field_, 1);

  auto* replace_button =
      search_row->AddChildView(std::make_unique<OriginQuickOpenButton>(
          base::BindRepeating(&OriginQuickOpenView::Submit,
                              base::Unretained(this),
                              OriginQuickOpenDisposition::kReplace),
          u"⌥  Replace"));
  StyleTextButton(replace_button, kSecondaryText, SK_ColorTRANSPARENT, 7);

  auto* split_button =
      search_row->AddChildView(std::make_unique<OriginQuickOpenButton>(
          base::BindRepeating(&OriginQuickOpenView::Submit,
                              base::Unretained(this),
                              OriginQuickOpenDisposition::kSplit),
          u"⇧  Split Screen"));
  StyleTextButton(split_button, SkColorSetRGB(31, 18, 66), kPurple, 7);

  auto* section_label =
      panel_->AddChildView(std::make_unique<views::Label>(u"Top Hits"));
  section_label->SetHorizontalAlignment(gfx::HorizontalAlignment::ALIGN_LEFT);
  section_label->SetEnabledColor(kSecondaryText);
  section_label->SetFontList(
      OriginQuickOpenFont(12, gfx::Font::Weight::MEDIUM));

  auto* primary_result =
      panel_->AddChildView(std::make_unique<OriginQuickOpenButton>(
          base::BindRepeating(&OriginQuickOpenView::Submit,
                              base::Unretained(this),
                              OriginQuickOpenDisposition::kNewPage),
          std::u16string()));
  primary_result_ = primary_result;
  primary_result_->SetPreferredSize(gfx::Size(0, 52));
  primary_result_->SetHorizontalAlignment(gfx::HorizontalAlignment::ALIGN_LEFT);
  primary_result_->SetEnabledTextColors(kPrimaryText);
  primary_result->SetOriginFont(
      OriginQuickOpenFont(14, gfx::Font::Weight::MEDIUM));
  primary_result_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(10, 14)));
  primary_result_->SetBackground(
      views::CreateRoundedRectBackground(kSelectedResultColor, 9));

  auto* command_row = panel_->AddChildView(
      std::make_unique<views::Label>(u"•••   Search all Origin commands"));
  command_row->SetHorizontalAlignment(gfx::HorizontalAlignment::ALIGN_LEFT);
  command_row->SetEnabledColor(kPrimaryText);
  command_row->SetFontList(OriginQuickOpenFont(13, gfx::Font::Weight::NORMAL));
  command_row->SetBorder(
      views::CreateEmptyBorder(gfx::Insets::TLBR(6, 10, 6, 10)));

  auto* keyboard_hint = panel_->AddChildView(
      std::make_unique<views::Label>(u"Enter  open page     Esc  close"));
  keyboard_hint->SetHorizontalAlignment(gfx::HorizontalAlignment::ALIGN_RIGHT);
  keyboard_hint->SetEnabledColor(kSecondaryText);
  keyboard_hint->SetFontList(
      OriginQuickOpenFont(11, gfx::Font::Weight::NORMAL));

  UpdatePrimaryResult(std::u16string());
}

OriginQuickOpenView::~OriginQuickOpenView() {
  if (search_field_) {
    search_field_->set_controller(nullptr);
  }
}

void OriginQuickOpenView::ShowAndFocus() {
  search_field_->SetText(std::u16string());
  UpdatePrimaryResult(std::u16string());
  SetVisible(true);
  search_field_->RequestFocus();
}

void OriginQuickOpenView::ContentsChanged(views::Textfield* sender,
                                          const std::u16string& new_contents) {
  if (sender == search_field_) {
    UpdatePrimaryResult(new_contents);
  }
}

bool OriginQuickOpenView::HandleKeyEvent(views::Textfield* sender,
                                         const ui::KeyEvent& key_event) {
  if (sender != search_field_ ||
      key_event.type() != ui::EventType::kKeyPressed) {
    return false;
  }

  if (key_event.key_code() == ui::VKEY_ESCAPE) {
    close_callback_.Run();
    return true;
  }
  if (key_event.key_code() == ui::VKEY_RETURN) {
    Submit(OriginQuickOpenDisposition::kNewPage);
    return true;
  }
  return false;
}

void OriginQuickOpenView::Layout(PassKey) {
  const gfx::Rect available = GetContentsBounds();
  const int panel_width =
      std::min(kPanelWidth, std::max(280, available.width() - 64));
  const int panel_height =
      std::min(kPanelHeight, std::max(220, available.height() - 64));
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
  submit_callback_.Run(std::u16string(search_field_->GetText()), disposition);
}

void OriginQuickOpenView::UpdatePrimaryResult(const std::u16string& input) {
  std::u16string cleaned = base::CollapseWhitespace(input, false);
  if (cleaned.empty()) {
    primary_result_->SetText(u"Open a new page");
    return;
  }
  constexpr size_t kMaxVisibleInput = 72;
  if (cleaned.size() > kMaxVisibleInput) {
    cleaned.resize(kMaxVisibleInput);
    cleaned.append(u"…");
  }
  primary_result_->SetText(base::StrCat({u"Search or open  “", cleaned, u"”"}));
}
