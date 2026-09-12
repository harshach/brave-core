/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/views/frame/origin_widgets/origin_widget_registry.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "brave/browser/ui/views/frame/origin_widgets/origin_calendar_widget.h"
#include "brave/browser/ui/views/frame/origin_widgets/origin_spotify_widget.h"
#include "brave/browser/ui/views/frame/origin_widgets/origin_youtube_widget.h"
#include "brave/components/vector_icons/vector_icons.h"

OriginWidgetDescriptor::OriginWidgetDescriptor(std::string id,
                                               std::u16string label,
                                               std::u16string hint,
                                               const gfx::VectorIcon& icon,
                                               Factory factory)
    : id(std::move(id)),
      label(std::move(label)),
      hint(std::move(hint)),
      icon(&icon),
      factory(std::move(factory)) {}

OriginWidgetDescriptor::OriginWidgetDescriptor(const OriginWidgetDescriptor&) =
    default;
OriginWidgetDescriptor& OriginWidgetDescriptor::operator=(
    const OriginWidgetDescriptor&) = default;
OriginWidgetDescriptor::~OriginWidgetDescriptor() = default;

const std::vector<OriginWidgetDescriptor>& GetOriginWidgetCatalog() {
  static const base::NoDestructor<std::vector<OriginWidgetDescriptor>> catalog(
      [] {
        std::vector<OriginWidgetDescriptor> widgets;
        widgets.emplace_back(
            "youtube", u"YouTube", u"Follows you", kOriginWidgetYoutubeIcon,
            base::BindRepeating(&OriginYouTubeWidgetView::Create));
        widgets.emplace_back(
            "spotify", u"Spotify", u"Web player", kOriginWidgetSpotifyIcon,
            base::BindRepeating(&OriginSpotifyWidgetView::Create));
        widgets.emplace_back(
            "calendar", u"Google Calendar", u"Day view",
            kOriginWidgetCalendarIcon,
            base::BindRepeating(&OriginCalendarWidgetView::Create));
        return widgets;
      }());
  return *catalog;
}

const OriginWidgetDescriptor* FindOriginWidget(std::string_view id) {
  const auto& catalog = GetOriginWidgetCatalog();
  const auto it = std::ranges::find(catalog, id, &OriginWidgetDescriptor::id);
  return it == catalog.end() ? nullptr : &*it;
}

std::unique_ptr<OriginWidgetView> CreateOriginWidget(
    std::string_view id,
    const OriginWidgetContext& context) {
  const auto* descriptor = FindOriginWidget(id);
  return descriptor ? descriptor->factory.Run(context) : nullptr;
}

std::vector<std::string> GetDefaultOriginWidgetIds() {
  std::vector<std::string> ids;
  for (const auto& descriptor : GetOriginWidgetCatalog()) {
    ids.push_back(descriptor.id);
  }
  return ids;
}
