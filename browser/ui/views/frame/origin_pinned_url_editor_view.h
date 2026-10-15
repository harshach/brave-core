// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_PINNED_URL_EDITOR_VIEW_H_
#define BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_PINNED_URL_EDITOR_VIEW_H_

#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"
#include "url/gurl.h"

namespace views {
class Label;
class Textfield;
}  // namespace views

// Edits the address a pinned page returns to, after Arc's "Edit Pinned Page":
// a small card over the page with the page's title and its home address,
// selected so typing replaces it.
class OriginPinnedUrlEditorView : public views::View,
                                  public views::TextfieldController {
  METADATA_HEADER(OriginPinnedUrlEditorView, views::View)

 public:
  using SaveCallback = base::OnceCallback<void(const GURL& home)>;

  // `on_close` runs each time the card hides.
  explicit OriginPinnedUrlEditorView(base::RepeatingClosure on_close);
  OriginPinnedUrlEditorView(const OriginPinnedUrlEditorView&) = delete;
  OriginPinnedUrlEditorView& operator=(const OriginPinnedUrlEditorView&) =
      delete;
  ~OriginPinnedUrlEditorView() override;

  // Shows the card for a pinned page whose home is `home` (may be empty) and
  // whose title is `title`, and focuses the field with the address selected.
  // The owner sizes this view to the page area, as it does the pickers.
  // `activation_key` is the key that opened the card; its character is kept
  // out of the field if it arrives after focus has moved there. `on_save`
  // runs with the new address when Enter accepts one.
  void Show(const GURL& home,
            const std::u16string& title,
            std::optional<ui::KeyboardCode> activation_key,
            SaveCallback on_save);
  // Hides the card without saving and runs `on_close`. Does nothing while
  // hidden.
  void Dismiss();

  views::Textfield* field_for_testing() { return field_; }
  // Confirms as Enter does; returns whether an address was saved.
  bool SubmitForTesting() { return Submit(); }

  // views::View:
  void Layout(PassKey) override;
  void OnThemeChanged() override;
  bool OnMousePressed(const ui::MouseEvent& event) override;

  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

 private:
  // The address to save, or an empty GURL when the field holds none.
  GURL GetEnteredHome() const;
  // Saves and hides the card, or says why the address can't be used.
  bool Submit();
  void SetErrorVisible(bool visible);
  void RefreshTheme();

  const base::RepeatingClosure on_close_;
  SaveCallback on_save_;
  GURL home_;
  // What the field showed on opening. Saved untouched, `home_` is kept as is.
  std::u16string home_text_;
  std::optional<ui::KeyboardCode> activation_key_to_suppress_;

  raw_ptr<views::View> panel_ = nullptr;
  raw_ptr<views::Label> heading_ = nullptr;
  raw_ptr<views::Label> page_title_ = nullptr;
  raw_ptr<views::View> field_container_ = nullptr;
  raw_ptr<views::Textfield> field_ = nullptr;
  raw_ptr<views::Label> error_ = nullptr;
  raw_ptr<views::Label> footer_ = nullptr;
  base::WeakPtrFactory<OriginPinnedUrlEditorView> weak_factory_{this};
};

#endif  // BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_PINNED_URL_EDITOR_VIEW_H_
