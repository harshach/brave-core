// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/views/frame/vertical_tabs/origin_owned_bubble.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/task/single_thread_task_runner.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"

OriginOwnedBubble::OriginOwnedBubble() = default;

OriginOwnedBubble::~OriginOwnedBubble() {
  weak_factory_.InvalidateWeakPtrs();
  // The widget must go before the delegate it points at.
  widget_.reset();
  delegate_.reset();
}

views::Widget* OriginOwnedBubble::Create(
    std::unique_ptr<views::BubbleDialogDelegate> delegate,
    base::OnceClosure on_closed) {
  if (widget_) {
    // Runs OnClosed() synchronously, which hands the old pair off for
    // deletion before this one replaces it.
    widget_->CloseNow();
  }
  delegate_ = std::move(delegate);
  on_closed_ = std::move(on_closed);
  widget_ = views::BubbleDialogDelegate::CreateBubble(
      delegate_.get(),
      base::BindOnce(&OriginOwnedBubble::OnClosed, weak_factory_.GetWeakPtr()));
  return widget_.get();
}

void OriginOwnedBubble::Close() {
  if (widget_) {
    widget_->Close();
  }
}

void OriginOwnedBubble::OnClosed(views::Widget::ClosedReason reason) {
  // Stop watching the anchor now: until the delete below runs, a re-theme of
  // the anchor's window would reach a bubble that no longer has a widget.
  delegate_->SetAnchorView(nullptr);
  // This runs inside the widget's own close, so delete it afterwards, widget
  // first since the delegate must outlive it.
  auto task_runner = base::SingleThreadTaskRunner::GetCurrentDefault();
  task_runner->DeleteSoon(FROM_HERE, widget_.release());
  task_runner->DeleteSoon(FROM_HERE, delegate_.release());
  if (on_closed_) {
    std::move(on_closed_).Run();
  }
}
