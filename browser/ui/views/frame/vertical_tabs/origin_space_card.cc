// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/views/frame/vertical_tabs/origin_space_card.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "brave/browser/ui/tabs/origin_space_theme.h"
#include "brave/browser/ui/views/frame/vertical_tabs/origin_owned_bubble.h"
#include "brave/components/vector_icons/vector_icons.h"
#include "cc/paint/paint_flags.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/bubble/bubble_frame_view.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/widget/widget.h"

namespace {

constexpr int kCardWidth = 296;
constexpr int kSwatchSize = 26;
constexpr int kSwatchSlot = 32;
constexpr SkColor kCardSurface = SkColorSetRGB(0x1C, 0x1F, 0x25);
constexpr SkColor kPrimaryText = SkColorSetRGB(0xF5, 0xF5, 0xF6);
constexpr SkColor kSecondaryText = SkColorSetRGB(0x94, 0x96, 0x9C);
constexpr SkColor kLabelText = SkColorSetRGB(0xA4, 0xA7, 0xAE);
constexpr SkColor kMutedText = SkColorSetRGB(0x8A, 0x8D, 0x94);
constexpr SkColor kDivider = SkColorSetARGB(0x14, 0xFF, 0xFF, 0xFF);
constexpr SkColor kRowHover = SkColorSetARGB(0x10, 0xFF, 0xFF, 0xFF);
constexpr SkColor kSegmentTrack = SkColorSetARGB(0x0F, 0xFF, 0xFF, 0xFF);
constexpr SkColor kSegmentSelected = SkColorSetARGB(0x21, 0xFF, 0xFF, 0xFF);
constexpr SkColor kDanger = SkColorSetRGB(0xF9, 0x70, 0x66);

struct IntensityChoice {
  OriginSpaceThemeIntensity value;
  std::u16string_view label;
};

constexpr IntensityChoice kIntensities[] = {
    {OriginSpaceThemeIntensity::kSubtle, u"Subtle"},
    {OriginSpaceThemeIntensity::kRich, u"Rich"},
    {OriginSpaceThemeIntensity::kVivid, u"Vivid"},
};

gfx::FontList CardFont(int size, gfx::Font::Weight weight) {
#if BUILDFLAG(IS_MAC)
  constexpr char kFamily[] = "SF Pro Text";
#elif BUILDFLAG(IS_WIN)
  constexpr char kFamily[] = "Segoe UI";
#else
  constexpr char kFamily[] = "Inter";
#endif
  return gfx::FontList({kFamily}, gfx::Font::NORMAL, size, weight);
}

std::unique_ptr<views::Label> MakeLabel(std::u16string text,
                                        int size,
                                        gfx::Font::Weight weight,
                                        SkColor color) {
  auto label = std::make_unique<views::Label>(std::move(text));
  label->SetFontList(CardFont(size, weight));
  label->SetEnabledColor(color);
  label->SetBackgroundColor(kCardSurface);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  return label;
}

std::unique_ptr<views::View> MakeDivider() {
  auto divider = std::make_unique<views::View>();
  divider->SetPreferredSize(gfx::Size(0, 1));
  divider->SetBackground(views::CreateSolidBackground(kDivider));
  return divider;
}

void FillRoundRect(gfx::Canvas* canvas,
                   const gfx::RectF& bounds,
                   float radius,
                   SkColor color) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setColor(color);
  canvas->DrawRoundRect(bounds, radius, flags);
}

// A menu-like row: icon and label, with a soft fill on hover.
class ActionRow : public views::LabelButton {
  METADATA_HEADER(ActionRow, views::LabelButton)

 public:
  ActionRow(PressedCallback callback,
            const gfx::VectorIcon& icon,
            std::u16string text,
            SkColor color)
      : LabelButton(std::move(callback), std::move(text)) {
    SetImageModel(STATE_NORMAL,
                  ui::ImageModel::FromVectorIcon(icon, color, 16));
    SetImageModel(STATE_DISABLED, ui::ImageModel::FromVectorIcon(
                                      icon, SkColorSetA(color, 0x66), 16));
    SetTextColor(STATE_NORMAL, color);
    SetTextColor(STATE_HOVERED, color);
    SetTextColor(STATE_PRESSED, color);
    SetTextColor(STATE_DISABLED, SkColorSetA(color, 0x66));
    label()->SetFontList(CardFont(14, gfx::Font::Weight::NORMAL));
    SetImageLabelSpacing(12);
    SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 6)));
    SetPreferredSize(gfx::Size(0, 34));
    SetFocusBehavior(FocusBehavior::ALWAYS);
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    if (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED ||
        HasFocus()) {
      FillRoundRect(canvas, gfx::RectF(GetLocalBounds()), 8, kRowHover);
    }
  }
};

BEGIN_METADATA(ActionRow)
END_METADATA

}  // namespace

// The Space's icon on its theme's fill, as the rail shows it when selected.
class OriginSpaceCard::IconTile : public views::View {
  METADATA_HEADER(IconTile, views::View)

 public:
  IconTile() { SetPreferredSize(gfx::Size(40, 40)); }

  void Set(const gfx::VectorIcon* icon, const origin_space_theme::Colors& c) {
    icon_ = icon;
    colors_ = c;
    SchedulePaint();
  }

  void OnPaint(gfx::Canvas* canvas) override {
    gfx::RectF bounds(GetLocalBounds());
    FillRoundRect(canvas, bounds, 10, colors_.selection);
    cc::PaintFlags ring;
    ring.setAntiAlias(true);
    ring.setStyle(cc::PaintFlags::kStroke_Style);
    ring.setStrokeWidth(1);
    ring.setColor(colors_.border);
    bounds.Inset(0.5f);
    canvas->DrawRoundRect(bounds, 9.5f, ring);
    if (icon_) {
      const gfx::ImageSkia image =
          ui::ImageModel::FromVectorIcon(*icon_, colors_.accent, 20)
              .Rasterize(GetColorProvider());
      canvas->DrawImageInt(image, (width() - image.width()) / 2,
                           (height() - image.height()) / 2);
    }
  }

 private:
  raw_ptr<const gfx::VectorIcon> icon_ = nullptr;
  origin_space_theme::Colors colors_{};
};

BEGIN_METADATA(OriginSpaceCard, IconTile)
END_METADATA

// One theme's colour chip. The chosen one gets a gap and an outer ring.
class OriginSpaceCard::Swatch : public views::Button {
  METADATA_HEADER(Swatch, views::Button)

 public:
  Swatch(PressedCallback callback, const origin_space_theme::Theme& theme)
      : Button(std::move(callback)), theme_id_(theme.id) {
    SetPreferredSize(gfx::Size(kSwatchSlot, kSwatchSlot));
    std::u16string name(theme.name);
    SetTooltipText(name);
    GetViewAccessibility().SetName(name + u" theme");
    color_ = origin_space_theme::GetColors(
                 theme.id, OriginSpaceThemeIntensity::kRich, /*dark=*/true)
                 .swatch;
  }

  std::string_view theme_id() const { return theme_id_; }

  void SetChosen(bool chosen) {
    if (chosen_ != chosen) {
      chosen_ = chosen;
      SchedulePaint();
    }
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const gfx::PointF center(width() / 2.0f, height() / 2.0f);
    const float radius = kSwatchSize / 2.0f;
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kFill_Style);
    if (chosen_ || GetState() == STATE_HOVERED || HasFocus()) {
      flags.setColor(chosen_ ? kPrimaryText : SkColorSetA(kPrimaryText, 0x40));
      canvas->DrawCircle(center, radius + 3.5f, flags);
      flags.setColor(kCardSurface);
      canvas->DrawCircle(center, radius + 2.0f, flags);
    }
    flags.setColor(color_);
    canvas->DrawCircle(center, radius, flags);
  }

 private:
  const std::string_view theme_id_;
  SkColor color_;
  bool chosen_ = false;
};

BEGIN_METADATA(OriginSpaceCard, Swatch)
END_METADATA

// One option of the intensity control.
class OriginSpaceCard::Segment : public views::LabelButton {
  METADATA_HEADER(Segment, views::LabelButton)

 public:
  Segment(PressedCallback callback, const IntensityChoice& choice)
      : LabelButton(std::move(callback), std::u16string(choice.label)),
        value_(choice.value) {
    label()->SetFontList(CardFont(12, gfx::Font::Weight::MEDIUM));
    SetHorizontalAlignment(gfx::ALIGN_CENTER);
    SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 9)));
    SetMinSize(gfx::Size(0, 22));
    SetMaxSize(gfx::Size(0, 22));
    SetFocusBehavior(FocusBehavior::ALWAYS);
    SetChosen(false);
  }

  OriginSpaceThemeIntensity value() const { return value_; }

  void SetChosen(bool chosen) {
    chosen_ = chosen;
    const SkColor color = chosen ? kPrimaryText : kMutedText;
    for (auto state : {STATE_NORMAL, STATE_HOVERED, STATE_PRESSED}) {
      SetTextColor(state, color);
    }
    SchedulePaint();
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    if (chosen_ || HasFocus()) {
      FillRoundRect(canvas, gfx::RectF(GetLocalBounds()), 6,
                    chosen_ ? kSegmentSelected : kRowHover);
    }
  }

 private:
  const OriginSpaceThemeIntensity value_;
  bool chosen_ = false;
};

BEGIN_METADATA(OriginSpaceCard, Segment)
END_METADATA

OriginSpaceCard::Params::Params() = default;
OriginSpaceCard::Params::Params(Params&&) = default;
OriginSpaceCard::Params& OriginSpaceCard::Params::operator=(Params&&) = default;
OriginSpaceCard::Params::~Params() = default;

// static
views::Widget* OriginSpaceCard::Show(OriginOwnedBubble& owner,
                                     views::View* anchor,
                                     Params params,
                                     base::OnceClosure on_closed) {
  auto bubble = std::make_unique<views::BubbleDialogDelegate>(
      anchor, views::BubbleBorder::BOTTOM_CENTER,
      views::BubbleBorder::STANDARD_SHADOW, /*autosize=*/true);
  auto* bubble_ptr = bubble.get();
  bubble->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  bubble->SetShowTitle(false);
  bubble->SetShowCloseButton(false);
  bubble->SetAccessibleTitle(u"Manage " + base::UTF8ToUTF16(params.space.name));
  bubble->set_adjust_if_offscreen(true);
  bubble->set_close_on_deactivate(true);
  bubble->set_fixed_width(kCardWidth);
  bubble->set_margins(gfx::Insets::TLBR(14, 14, 10, 14));
  bubble->SetBackgroundColor(kCardSurface);
  bubble->SetContentsView(std::make_unique<OriginSpaceCard>(std::move(params)));
  auto* widget = owner.Create(std::move(bubble), std::move(on_closed));
  auto* frame = bubble_ptr->GetBubbleFrameView();
  frame->SetRoundedCorners(gfx::RoundedCornersF(16));
  frame->SetDisplayVisibleArrow(true);
  frame->bubble_border()->set_draw_border_stroke(true);
  widget->Show();
  return widget;
}

OriginSpaceCard::OriginSpaceCard(Params params) : params_(std::move(params)) {
  SetBackground(views::CreateSolidBackground(kCardSurface));
  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 12));

  // Header: icon, name and page count, and the key that selects the Space.
  auto* header = AddChildView(std::make_unique<views::BoxLayoutView>());
  header->SetBetweenChildSpacing(12);
  header->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  icon_tile_ = header->AddChildView(std::make_unique<IconTile>());
  auto* titles = header->AddChildView(std::make_unique<views::BoxLayoutView>());
  titles->SetOrientation(views::BoxLayout::Orientation::kVertical);
  titles->SetBetweenChildSpacing(2);
  titles->AddChildView(MakeLabel(base::UTF8ToUTF16(params_.space.name), 15,
                                 gfx::Font::Weight::SEMIBOLD, kPrimaryText));
  std::u16string pages = base::NumberToString16(params_.page_count);
  pages.append(params_.page_count == 1 ? u" page" : u" pages");
  titles->AddChildView(
      MakeLabel(pages, 13, gfx::Font::Weight::NORMAL, kSecondaryText));
  header->SetFlexForView(titles, 1);
  if (params_.shortcut > 0) {
    auto* key = header->AddChildView(
        MakeLabel(base::NumberToString16(params_.shortcut), 12,
                  gfx::Font::Weight::SEMIBOLD, kLabelText));
    key->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    key->SetPreferredSize(gfx::Size(22, 22));
    key->SetBackground(views::CreateRoundedRectBackground(kDivider, 6));
    key->SetBackgroundColor(SkColorSetRGB(0x2A, 0x2D, 0x33));
    key->SetTooltipText(u"Press " + base::NumberToString16(params_.shortcut) +
                        u" to switch to this Space");
  }

  AddChildView(MakeDivider());

  // Theme: the chosen theme's name, then one chip per theme.
  auto* theme_row = AddChildView(std::make_unique<views::BoxLayoutView>());
  auto* theme_label = theme_row->AddChildView(
      MakeLabel(u"Theme", 13, gfx::Font::Weight::SEMIBOLD, kLabelText));
  theme_row->SetFlexForView(theme_label, 1);
  theme_name_ = theme_row->AddChildView(
      MakeLabel(u"", 13, gfx::Font::Weight::NORMAL, kSecondaryText));

  auto* swatch_row = AddChildView(std::make_unique<views::BoxLayoutView>());
  swatch_row->SetMainAxisAlignment(
      views::BoxLayout::MainAxisAlignment::kCenter);
  for (const auto& theme : origin_space_theme::GetThemes()) {
    swatches_.push_back(swatch_row->AddChildView(std::make_unique<Swatch>(
        base::BindRepeating(&OriginSpaceCard::SelectTheme,
                            base::Unretained(this), std::string(theme.id)),
        theme)));
  }

  // Intensity is shared by every Space.
  auto* intensity_row = AddChildView(std::make_unique<views::BoxLayoutView>());
  intensity_row->SetCrossAxisAlignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  auto* intensity_label = intensity_row->AddChildView(
      MakeLabel(u"Intensity", 13, gfx::Font::Weight::SEMIBOLD, kLabelText));
  intensity_row->SetFlexForView(intensity_label, 1);
  auto* track =
      intensity_row->AddChildView(std::make_unique<views::BoxLayoutView>());
  track->SetInsideBorderInsets(gfx::Insets(2));
  track->SetBetweenChildSpacing(2);
  track->SetBackground(views::CreateRoundedRectBackground(kSegmentTrack, 8));
  for (const auto& choice : kIntensities) {
    segments_.push_back(track->AddChildView(std::make_unique<Segment>(
        base::BindRepeating(&OriginSpaceCard::SelectIntensity,
                            base::Unretained(this), choice.value),
        choice)));
  }

  AddChildView(MakeDivider());

  auto* actions = AddChildView(std::make_unique<views::BoxLayoutView>());
  actions->SetOrientation(views::BoxLayout::Orientation::kVertical);
  actions->AddChildView(std::make_unique<ActionRow>(
      base::BindRepeating(&OriginSpaceCard::RunAction, base::Unretained(this),
                          params_.rename),
      kLeoEditBoxIcon, u"Rename", kPrimaryText));
  actions->AddChildView(std::make_unique<ActionRow>(
      base::BindRepeating(&OriginSpaceCard::RunAction, base::Unretained(this),
                          params_.change_icon),
      kLeoGrid04Icon, u"Change icon", kPrimaryText));
  auto* delete_row = actions->AddChildView(std::make_unique<ActionRow>(
      base::BindRepeating(&OriginSpaceCard::RunAction, base::Unretained(this),
                          params_.delete_space),
      kLeoTrashIcon, u"Delete space", kDanger));
  delete_row->SetEnabled(params_.can_delete);
  if (!params_.can_delete) {
    delete_row->SetTooltipText(u"A window always keeps at least one Space");
  }

  Refresh();
}

OriginSpaceCard::~OriginSpaceCard() = default;

void OriginSpaceCard::SelectTheme(const std::string& theme) {
  params_.space.theme = theme;
  if (params_.set_theme) {
    params_.set_theme.Run(theme);
  }
  Refresh();
}

void OriginSpaceCard::SelectIntensity(OriginSpaceThemeIntensity intensity) {
  params_.intensity = intensity;
  if (params_.set_intensity) {
    params_.set_intensity.Run(intensity);
  }
  Refresh();
}

void OriginSpaceCard::RunAction(base::RepeatingClosure action) {
  if (auto* widget = GetWidget()) {
    widget->Close();
  }
  // Deleting a Space rebuilds the rail this card is anchored to, so let the
  // card finish handling the click before anything acts on it.
  if (action) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, action);
  }
}

void OriginSpaceCard::Refresh() {
  const auto& theme = origin_space_theme::GetTheme(params_.space.theme);
  icon_tile_->Set(params_.icon,
                  origin_space_theme::GetColors(theme.id, params_.intensity,
                                                /*dark=*/true));
  theme_name_->SetText(std::u16string(theme.name));
  for (auto& swatch : swatches_) {
    swatch->SetChosen(swatch->theme_id() == theme.id);
  }
  for (auto& segment : segments_) {
    segment->SetChosen(segment->value() == params_.intensity);
  }
}

BEGIN_METADATA(OriginSpaceCard)
END_METADATA
