/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_WIDGETS_ORIGIN_WIDGET_PANEL_VIEW_H_
#define BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_WIDGETS_ORIGIN_WIDGET_PANEL_VIEW_H_

#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "brave/browser/ui/views/frame/origin_widgets/origin_widget.h"
#include "components/prefs/pref_change_registrar.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/view.h"

class Browser;
class OriginMediaMonitor;

namespace views {
class ImageView;
class Label;
class MenuRunner;
}  // namespace views

// The right-hand widgets panel: a mirror of the spaces panel on the other
// edge of the window.
//
// The panel owns no widget-specific knowledge. It reads an ordered list of
// widget ids from prefs and asks the registry to build each one, so adding a
// widget never touches this file.
class OriginWidgetPanelView : public views::View,
                              public ui::SimpleMenuModel::Delegate {
  METADATA_HEADER(OriginWidgetPanelView, views::View)

 public:
  static constexpr int kPanelWidth = 280;

  OriginWidgetPanelView(Browser* browser, OriginMediaMonitor* media_monitor);
  OriginWidgetPanelView(const OriginWidgetPanelView&) = delete;
  OriginWidgetPanelView& operator=(const OriginWidgetPanelView&) = delete;
  ~OriginWidgetPanelView() override;

  // Panel visibility is a profile preference so a new window opens the way the
  // last one looked.
  bool IsPanelOpen() const;
  void SetPanelOpen(bool open);
  void TogglePanel();

  // views::View:
  void OnThemeChanged() override;
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdChecked(int command_id) const override;
  bool IsCommandIdEnabled(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

 private:
  std::vector<std::string> GetEnabledWidgetIds() const;
  void RebuildWidgets();
  void ShowWidgetPicker();
  void UpdatePanelVisibility();

  std::unique_ptr<ui::SimpleMenuModel> picker_model_;
  std::unique_ptr<views::MenuRunner> picker_runner_;
  PrefChangeRegistrar pref_registrar_;

  raw_ptr<Browser> browser_ = nullptr;
  raw_ptr<OriginMediaMonitor> media_monitor_ = nullptr;
  raw_ptr<views::View> card_container_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::Label> subtitle_label_ = nullptr;
  raw_ptr<views::ImageView> header_icon_ = nullptr;
  raw_ptr<views::Label> key_hint_label_ = nullptr;
  raw_ptr<views::View> add_button_ = nullptr;
  std::vector<raw_ptr<OriginWidgetView>> widgets_;
};

#endif  // BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_WIDGETS_ORIGIN_WIDGET_PANEL_VIEW_H_
