/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/views/frame/origin_widgets/origin_widget_panel_view.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "brave/browser/ui/color/brave_color_id.h"
#include "brave/browser/ui/views/frame/origin_widgets/origin_widget_controls.h"
#include "brave/browser/ui/views/frame/origin_widgets/origin_widget_registry.h"
#include "brave/browser/ui/views/frame/origin_widgets/origin_widget_style.h"
#include "brave/browser/workspaces/pref_names.h"
#include "brave/components/vector_icons/vector_icons.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "components/prefs/pref_service.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/view_class_properties.h"

namespace {

// Commands are catalog indices offset by one; zero is not a valid command id.
constexpr int kFirstWidgetCommandId = 1;

constexpr int kCardSpacing = 8;

}  // namespace

OriginWidgetPanelView::OriginWidgetPanelView(Browser* browser,
                                             OriginMediaMonitor* media_monitor)
    : browser_(browser), media_monitor_(media_monitor) {
  SetLayoutManager(std::make_unique<views::FlexLayout>())
      ->SetOrientation(views::LayoutOrientation::kVertical);
  // Mirrors the spaces panel's inset on the opposite edge.
  SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(0, 0, 8, 8)));

  auto* header = AddChildView(std::make_unique<views::View>());
  auto* header_layout =
      header->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::TLBR(7, 8, 5, 8), 7));
  header_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  header->SetPreferredSize(gfx::Size(0, origin_style::kPanelHeaderHeight));

  header_icon_ = header->AddChildView(std::make_unique<views::ImageView>());
  header_icon_->SetPreferredSize(gfx::Size(26, 26));

  auto* heading = header->AddChildView(std::make_unique<views::View>());
  heading->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  header_layout->SetFlexForView(heading, 1);
  title_label_ =
      heading->AddChildView(CreateOriginWidgetSectionLabel(u"Widgets"));
  title_label_->SetFontList(
      origin_style::ChromeFont(16, gfx::Font::Weight::SEMIBOLD));
  subtitle_label_ =
      heading->AddChildView(CreateOriginWidgetSubtitleLabel(std::u16string()));

  key_hint_label_ = header->AddChildView(CreateOriginWidgetSectionLabel(u"W"));
  key_hint_label_->SetFontList(
      origin_style::ChromeFont(10, gfx::Font::Weight::SEMIBOLD));
  key_hint_label_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(1, 5)));

  add_button_ = header->AddChildView(std::make_unique<OriginWidgetIconButton>(
      base::BindRepeating(&OriginWidgetPanelView::ShowWidgetPicker,
                          base::Unretained(this)),
      kLeoPlusAddIcon, u"Add widget"));

  auto* scroll = AddChildView(std::make_unique<views::ScrollView>());
  scroll->SetProperty(views::kMarginsKey,
                      gfx::Insets::TLBR(origin_style::kPanelContentTop -
                                            origin_style::kPanelHeaderHeight,
                                        0, 0, 0));
  scroll->SetDrawOverflowIndicator(false);
  scroll->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll->SetBackgroundColor(std::nullopt);
  scroll->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded)
          .WithWeight(1));

  card_container_ = scroll->SetContents(std::make_unique<views::View>());
  card_container_->SetLayoutManager(std::make_unique<views::FlexLayout>())
      ->SetOrientation(views::LayoutOrientation::kVertical)
      .SetCollapseMargins(true)
      .SetDefault(views::kMarginsKey,
                  gfx::Insets::TLBR(0, 0, kCardSpacing, 0));

  if (auto* prefs = browser_->GetProfile()->GetPrefs()) {
    pref_registrar_.Init(prefs);
    pref_registrar_.Add(
        kOriginWidgetsEnabledPref,
        base::BindRepeating(&OriginWidgetPanelView::RebuildWidgets,
                            base::Unretained(this)));
    pref_registrar_.Add(
        kOriginWidgetPanelVisiblePref,
        base::BindRepeating(&OriginWidgetPanelView::UpdatePanelVisibility,
                            base::Unretained(this)));
  }

  RebuildWidgets();
  SetVisible(IsPanelOpen());
}

OriginWidgetPanelView::~OriginWidgetPanelView() = default;

bool OriginWidgetPanelView::IsPanelOpen() const {
  return browser_->GetProfile()->GetPrefs()->GetBoolean(
      kOriginWidgetPanelVisiblePref);
}

void OriginWidgetPanelView::SetPanelOpen(bool open) {
  browser_->GetProfile()->GetPrefs()->SetBoolean(kOriginWidgetPanelVisiblePref,
                                                 open);
}

void OriginWidgetPanelView::UpdatePanelVisibility() {
  const bool open = IsPanelOpen();
  SetVisible(open);
  if (open) {
    // Widgets stay idle while hidden, so they need a nudge on the way back.
    for (auto& widget : widgets_) {
      widget->Refresh();
    }
  }
  if (parent()) {
    parent()->InvalidateLayout();
  }
}

void OriginWidgetPanelView::TogglePanel() {
  SetPanelOpen(!IsPanelOpen());
}

void OriginWidgetPanelView::OnThemeChanged() {
  views::View::OnThemeChanged();
  const bool dark = origin_style::IsDark(this);
  SetBackground(
      views::CreateSolidBackground(kColorBraveVerticalTabInactiveBackground));
  title_label_->SetEnabledColor(origin_style::PrimaryText(dark));
  subtitle_label_->SetEnabledColor(origin_style::MutedText(dark));
  header_icon_->SetImage(ui::ImageModel::FromVectorIcon(
      kLeoGrid04Icon, origin_style::SecondaryText(dark), 16));
  header_icon_->SetBackground(
      views::CreateRoundedRectBackground(origin_style::HoverFill(dark), 8));
  key_hint_label_->SetEnabledColor(origin_style::MutedText(dark));
  key_hint_label_->SetBackground(views::CreateRoundedRectBackground(
      origin_style::HoverFill(dark), 4));
}

gfx::Size OriginWidgetPanelView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  // The panel is a fixed column, like the spaces panel it mirrors; only its
  // height follows the window.
  return gfx::Size(kPanelWidth, 0);
}

bool OriginWidgetPanelView::IsCommandIdChecked(int command_id) const {
  const auto& catalog = GetOriginWidgetCatalog();
  const size_t index = static_cast<size_t>(command_id - kFirstWidgetCommandId);
  if (index >= catalog.size()) {
    return false;
  }
  return std::ranges::contains(GetEnabledWidgetIds(), catalog[index].id);
}

bool OriginWidgetPanelView::IsCommandIdEnabled(int command_id) const {
  return command_id >= kFirstWidgetCommandId &&
         static_cast<size_t>(command_id - kFirstWidgetCommandId) <
             GetOriginWidgetCatalog().size();
}

void OriginWidgetPanelView::ExecuteCommand(int command_id, int event_flags) {
  const auto& catalog = GetOriginWidgetCatalog();
  const size_t index = static_cast<size_t>(command_id - kFirstWidgetCommandId);
  if (index >= catalog.size()) {
    return;
  }
  const std::string& id = catalog[index].id;

  auto enabled = GetEnabledWidgetIds();
  if (auto it = std::ranges::find(enabled, id); it != enabled.end()) {
    enabled.erase(it);
  } else {
    enabled.push_back(id);
  }

  base::ListValue list;
  for (const auto& widget_id : enabled) {
    list.Append(widget_id);
  }
  browser_->GetProfile()->GetPrefs()->SetList(kOriginWidgetsEnabledPref,
                                           std::move(list));
}

std::vector<std::string> OriginWidgetPanelView::GetEnabledWidgetIds() const {
  const auto* prefs = browser_->GetProfile()->GetPrefs();
  if (prefs->FindPreference(kOriginWidgetsEnabledPref)->IsDefaultValue()) {
    return GetDefaultOriginWidgetIds();
  }
  const auto& stored = prefs->GetList(kOriginWidgetsEnabledPref);
  std::vector<std::string> ids;
  for (const auto& value : stored) {
    // Drop ids from a build that had widgets this one does not.
    if (value.is_string() && FindOriginWidget(value.GetString()) &&
        !std::ranges::contains(ids, value.GetString())) {
      ids.push_back(value.GetString());
    }
  }
  return ids;
}

void OriginWidgetPanelView::RebuildWidgets() {
  widgets_.clear();
  card_container_->RemoveAllChildViews();

  OriginWidgetContext context;
  context.browser = browser_;
  context.media_monitor = media_monitor_;
  context.prefs = browser_->GetProfile()->GetPrefs();

  for (const auto& id : GetEnabledWidgetIds()) {
    if (auto widget = CreateOriginWidget(id, context)) {
      auto* added = card_container_->AddChildView(std::move(widget));
      widgets_.push_back(added);
      // A widget is built empty; this is what gives it its first read of the
      // media monitor or its feed.
      added->Refresh();
    }
  }
  subtitle_label_->SetText(base::NumberToString16(widgets_.size()) +
                           (widgets_.size() == 1 ? u" widget" : u" widgets"));
  if (widgets_.empty()) {
    auto* empty = card_container_->AddChildView(
        CreateOriginWidgetSubtitleLabel(u"Use + to add a widget"));
    empty->SetEnabledColor(kColorBraveVerticalTabNTBTextColor);
    empty->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(12, 8)));
  }
  InvalidateLayout();
}

void OriginWidgetPanelView::ShowWidgetPicker() {
  picker_runner_.reset();
  picker_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  const auto& catalog = GetOriginWidgetCatalog();
  for (size_t index = 0; index < catalog.size(); ++index) {
    std::u16string label = catalog[index].label;
    if (!catalog[index].hint.empty()) {
      label.append(u"  ").append(catalog[index].hint);
    }
    picker_model_->AddCheckItem(
        static_cast<int>(index) + kFirstWidgetCommandId, label);
  }

  picker_runner_ = std::make_unique<views::MenuRunner>(
      picker_model_.get(), views::MenuRunner::HAS_MNEMONICS);
  picker_runner_->RunMenuAt(GetWidget(), nullptr,
                            add_button_->GetBoundsInScreen(),
                            views::MenuAnchorPosition::kTopRight,
                            ui::mojom::MenuSourceType::kNone);
}

BEGIN_METADATA(OriginWidgetPanelView)
END_METADATA
