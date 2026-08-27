// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_TEMPORARY_LINK_VIEW_H_
#define BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_TEMPORARY_LINK_VIEW_H_

#include <cstddef>
#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

class Browser;

namespace views {
class BubbleDialogDelegate;
class ImageButton;
class ImageView;
class Label;
class LabelButton;
class Widget;
}  // namespace views

// Native chrome for links opened by another application. The page is not part
// of any Space until the user keeps it.
class OriginTemporaryLinkView : public views::View {
  METADATA_HEADER(OriginTemporaryLinkView, views::View)

 public:
  static constexpr int kBarHeight = 44;

  explicit OriginTemporaryLinkView(Browser* browser);
  OriginTemporaryLinkView(const OriginTemporaryLinkView&) = delete;
  OriginTemporaryLinkView& operator=(const OriginTemporaryLinkView&) = delete;
  ~OriginTemporaryLinkView() override;

  void Update();
  void KeepSuggested();
  void KeepInSplit();
  void ReplaceCurrentPage();
  bool KeepInSpaceAtIndex(size_t index);
  void Discard();

  void SelectSpace(std::string space_id);
  void SetAlwaysOpenDomainHere(bool enabled);
  bool always_open_domain_here() const { return always_open_domain_here_; }
  const std::string& selected_space_id() const { return selected_space_id_; }
  base::WeakPtr<OriginTemporaryLinkView> GetWeakPtr();

 private:
  void OnThemeChanged() override;
  void RefreshTheme();
  void ShowSpacePicker();
  void OnSpacePickerClosed();
  void KeepInSelectedSpace();
  void RefreshSpaceChip();
  void BalanceSideContainers();

  raw_ptr<Browser> browser_ = nullptr;
  raw_ptr<views::View> leading_container_ = nullptr;
  raw_ptr<views::View> trailing_container_ = nullptr;
  raw_ptr<views::ImageView> incoming_link_icon_ = nullptr;
  raw_ptr<views::Label> open_in_label_ = nullptr;
  raw_ptr<views::LabelButton> space_button_ = nullptr;
  raw_ptr<views::ImageView> favicon_view_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::Label> count_label_ = nullptr;
  raw_ptr<views::ImageButton> discard_button_ = nullptr;
  std::string selected_space_id_;
  std::string selected_domain_;
  bool always_open_domain_here_ = false;
  std::unique_ptr<views::BubbleDialogDelegate> space_picker_delegate_;
  std::unique_ptr<views::Widget> space_picker_widget_;
  base::WeakPtrFactory<OriginTemporaryLinkView> weak_factory_{this};
};

#endif  // BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_TEMPORARY_LINK_VIEW_H_
