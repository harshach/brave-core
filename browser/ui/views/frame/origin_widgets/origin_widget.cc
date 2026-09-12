/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/views/frame/origin_widgets/origin_widget.h"

#include <utility>

#include "brave/browser/ui/views/frame/origin_widgets/origin_widget_style.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/background.h"
#include "ui/views/border.h"

OriginWidgetView::OriginWidgetView(std::string widget_id)
    : widget_id_(std::move(widget_id)) {}

OriginWidgetView::~OriginWidgetView() = default;

void OriginWidgetView::Refresh() {}

void OriginWidgetView::OnThemeChanged() {
  views::View::OnThemeChanged();
  // Every widget is the same soft card, so the frame is drawn here rather than
  // repeated in each subclass.
  const bool dark = origin_style::IsDark(this);
  SetBackground(views::CreateRoundedRectBackground(
      origin_style::CardBackground(dark), origin_style::kCardCornerRadius));
  SetBorder(views::CreateRoundedRectBorder(
      1, origin_style::kCardCornerRadius, origin_style::CardBorder(dark)));
}

BEGIN_METADATA(OriginWidgetView)
END_METADATA
