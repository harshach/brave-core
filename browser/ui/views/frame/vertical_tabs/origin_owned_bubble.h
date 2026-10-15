// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_VIEWS_FRAME_VERTICAL_TABS_ORIGIN_OWNED_BUBBLE_H_
#define BRAVE_BROWSER_UI_VIEWS_FRAME_VERTICAL_TABS_ORIGIN_OWNED_BUBBLE_H_

#include <memory>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "ui/views/widget/widget.h"

namespace views {
class BubbleDialogDelegate;
}  // namespace views

// Owns a bubble's delegate and widget together and deletes both once the
// bubble closes. A delegate nothing owns outlives its widget and keeps
// observing the anchor window, which then crashes the next time that window
// re-themes.
class OriginOwnedBubble {
 public:
  OriginOwnedBubble();
  OriginOwnedBubble(const OriginOwnedBubble&) = delete;
  OriginOwnedBubble& operator=(const OriginOwnedBubble&) = delete;
  ~OriginOwnedBubble();

  // Creates the bubble's widget without showing it, so the caller can style
  // its frame first. A bubble already held here is closed at once.
  views::Widget* Create(std::unique_ptr<views::BubbleDialogDelegate> delegate,
                        base::OnceClosure on_closed = base::OnceClosure());

  // Closes the bubble; it is deleted once the close finishes.
  void Close();

  views::Widget* widget() const { return widget_.get(); }
  explicit operator bool() const { return !!widget_; }

 private:
  void OnClosed(views::Widget::ClosedReason reason);

  std::unique_ptr<views::BubbleDialogDelegate> delegate_;
  std::unique_ptr<views::Widget> widget_;
  base::OnceClosure on_closed_;
  base::WeakPtrFactory<OriginOwnedBubble> weak_factory_{this};
};

#endif  // BRAVE_BROWSER_UI_VIEWS_FRAME_VERTICAL_TABS_ORIGIN_OWNED_BUBBLE_H_
