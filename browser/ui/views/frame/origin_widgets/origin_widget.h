/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_WIDGETS_ORIGIN_WIDGET_H_
#define BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_WIDGETS_ORIGIN_WIDGET_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

class Browser;
class OriginMediaMonitor;
class PrefService;

// Everything a widget is allowed to reach. Passing one struct keeps widget
// constructors uniform, which is what makes the registry able to build any of
// them from a single factory signature.
struct OriginWidgetContext {
  raw_ptr<Browser> browser = nullptr;
  raw_ptr<OriginMediaMonitor> media_monitor = nullptr;
  raw_ptr<PrefService> prefs = nullptr;
};

// Base class for one card in the widgets panel.
//
// Adding a widget means subclassing this, giving it a factory, and listing it
// in the catalog in origin_widget_registry.cc. The panel handles ordering,
// enablement, persistence and the picker generically, so a new widget never
// touches panel code.
class OriginWidgetView : public views::View {
  METADATA_HEADER(OriginWidgetView, views::View)

 public:
  explicit OriginWidgetView(std::string widget_id);
  OriginWidgetView(const OriginWidgetView&) = delete;
  OriginWidgetView& operator=(const OriginWidgetView&) = delete;
  ~OriginWidgetView() override;

  const std::string& widget_id() const { return widget_id_; }

  // Asks the widget to re-read whatever it is backed by. Called once when the
  // card is built and again whenever the panel becomes visible, so a widget
  // that polls can stay idle while hidden.
  virtual void Refresh();

  // views::View:
  void OnThemeChanged() override;

 private:
  std::string widget_id_;
};

#endif  // BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_WIDGETS_ORIGIN_WIDGET_H_
