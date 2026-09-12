/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_WIDGETS_ORIGIN_WIDGET_REGISTRY_H_
#define BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_WIDGETS_ORIGIN_WIDGET_REGISTRY_H_

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "brave/browser/ui/views/frame/origin_widgets/origin_widget.h"

namespace gfx {
struct VectorIcon;
}

// One entry in the widget catalog.
//
// To add a widget: write an OriginWidgetView subclass with a static Create()
// taking an OriginWidgetContext, then append a descriptor for it in
// origin_widget_registry.cc. Nothing else needs to change — the panel, the
// picker and the persisted ordering are all driven from the catalog.
struct OriginWidgetDescriptor {
  using Factory = base::RepeatingCallback<std::unique_ptr<OriginWidgetView>(
      const OriginWidgetContext&)>;

  OriginWidgetDescriptor(std::string id,
                         std::u16string label,
                         std::u16string hint,
                         const gfx::VectorIcon& icon,
                         Factory factory);
  OriginWidgetDescriptor(const OriginWidgetDescriptor&);
  OriginWidgetDescriptor& operator=(const OriginWidgetDescriptor&);
  ~OriginWidgetDescriptor();

  std::string id;
  std::u16string label;
  // Shown greyed next to the label in the picker, e.g. "Follows you".
  std::u16string hint;
  raw_ptr<const gfx::VectorIcon> icon = nullptr;
  Factory factory;
};

const std::vector<OriginWidgetDescriptor>& GetOriginWidgetCatalog();
const OriginWidgetDescriptor* FindOriginWidget(std::string_view id);
std::unique_ptr<OriginWidgetView> CreateOriginWidget(
    std::string_view id,
    const OriginWidgetContext& context);

// The widgets a profile starts with, in panel order.
std::vector<std::string> GetDefaultOriginWidgetIds();

#endif  // BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_WIDGETS_ORIGIN_WIDGET_REGISTRY_H_
