/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_QUICK_OPEN_VIEW_H_
#define BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_QUICK_OPEN_VIEW_H_

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/time/time.h"
#include "components/omnibox/browser/autocomplete_controller.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"
#include "url/gurl.h"

class OriginQuickOpenResultButton;
class OriginQuickOpenSpaceChip;
class OriginQuickOpenTextButton;
class Browser;
class Profile;

namespace favicon_base {
struct FaviconRawBitmapResult;
}  // namespace favicon_base

namespace gfx {
class Image;
struct VectorIcon;
}  // namespace gfx

namespace history {
class QueryResults;
}  // namespace history

namespace image_fetcher {
struct RequestMetadata;
}  // namespace image_fetcher

namespace ui {
class KeyEvent;
class MouseEvent;
}  // namespace ui

namespace views {
class ImageView;
class Label;
class Textfield;
} // namespace views

enum class OriginQuickOpenDisposition {
  kNewPage,
  kReplace,
  kSplit,
};

// A resolved Quick Open choice. `destination_url` is populated by Chromium's
// native omnibox providers when a suggestion is selected. `switch_to_tab`
// distinguishes an existing-tab result from an otherwise identical history or
// URL result.
struct OriginQuickOpenSelection {
  std::u16string input;
  GURL destination_url;
  std::string space_id;
  std::string destination_space_id;
  bool switch_to_tab = false;
};

// A window-scoped Quick Open surface for Brave Origin. It deliberately lives
// in browser chrome instead of the renderer so single-key navigation remains
// inactive while the user types and all opened pages retain normal Brave
// protections and tab semantics.
class OriginQuickOpenView : public views::View,
                            public views::TextfieldController,
                            public AutocompleteController::Observer {
  METADATA_HEADER(OriginQuickOpenView, views::View)

public:
  using SubmitCallback = base::RepeatingCallback<void(
      OriginQuickOpenSelection, OriginQuickOpenDisposition)>;

  OriginQuickOpenView(Browser *browser, SubmitCallback submit_callback,
                      base::RepeatingClosure command_callback,
                      base::RepeatingClosure close_callback);
  OriginQuickOpenView(const OriginQuickOpenView &) = delete;
  OriginQuickOpenView &operator=(const OriginQuickOpenView &) = delete;
  ~OriginQuickOpenView() override;

  // `activation_key` is consumed after focus moves into the search field. A
  // native single-key press can otherwise open Quick Open and then insert that
  // same character into the newly focused field. Modifier accelerators such as
  // Ctrl/Cmd+T pass no activation key.
  void
  ShowAndFocus(std::optional<ui::KeyboardCode> activation_key = std::nullopt);
  void Dismiss();

  // views::TextfieldController:
  void ContentsChanged(views::Textfield *sender,
                       const std::u16string &new_contents) override;
  bool HandleKeyEvent(views::Textfield *sender,
                      const ui::KeyEvent &key_event) override;

  // AutocompleteController::Observer:
  void OnResultChanged(AutocompleteController *controller,
                       bool default_match_changed) override;

  // views::View:
  void Layout(PassKey) override;
  void OnThemeChanged() override;
  bool OnMousePressed(const ui::MouseEvent &event) override;

private:
  friend class BraveBrowserViewTest_OriginQuickOpenPromotesDirectSite_Test;
  friend class BraveBrowserViewTest_OriginQuickOpenCompletesPartialDomain_Test;
  friend class BraveBrowserViewTest_OriginQuickOpenShowsTopHits_Test;
  friend class BraveBrowserViewTest_OriginQuickOpenPrefersOpenPage_Test;
  friend class BraveBrowserViewTest_OriginQuickOpenRanksMatchingHistory_Test;
  friend class
      BraveBrowserViewTest_OriginQuickOpenNumberTypesUntilResultNavigation_Test;
  friend class BraveBrowserViewTest_OriginQuickOpenFetchesMissingFavicon_Test;

  static constexpr size_t kSectionCount = 3;

  struct Result {
    std::u16string title;
    std::u16string subtitle;
    std::u16string badge;
    GURL destination_url;
    std::string space_id;
    bool switch_to_tab = false;
    bool is_search = false;
    ui::ImageModel icon_model;
    raw_ptr<const gfx::VectorIcon> icon = nullptr;
  };

  struct TimedResult {
    Result result;
    base::Time last_active;
  };

  void Submit(OriginQuickOpenDisposition disposition);
  void SubmitResult(const Result &result,
                    OriginQuickOpenDisposition disposition);
  void SubmitToSpace(size_t space_index);
  void SetDisposition(OriginQuickOpenDisposition disposition);
  OriginQuickOpenDisposition GetEffectiveDisposition() const;
  void UpdateDispositionButtons();
  void OnResultPressed(size_t section, size_t index);
  void StartAutocomplete(const std::u16string &input);
  void RebuildResults();
  void ApplyInlineAutocomplete();
  void LoadZeroStateData();
  void QueryRecentHistory();
  void QueryHistoryForInput(const std::u16string& input);
  void OnHistoryQueryComplete(std::u16string requested_input,
                              history::QueryResults results);
  ui::ImageModel GetFaviconModelForURL(const GURL& url) const;
  void RequestFavicon(const GURL& url, bool allow_network_fetch = false);
  void OnFaviconLoaded(
      const GURL& url,
      const favicon_base::FaviconRawBitmapResult& bitmap_result);
  void RequestFaviconFromNetwork(const GURL& url,
                                 bool allow_known_site_override = true);
  void OnNetworkFaviconLoaded(
      const GURL& url,
      bool used_known_site_override,
      const gfx::Image& image,
      const image_fetcher::RequestMetadata& request_metadata);
  void ApplyFavicon(const GURL& url, const gfx::Image& favicon);
  void UpdateResultRows();
  void UpdateSearchIcon();
  void RefreshTheme();
  void RebuildSpaceControls();
  void SelectResult(size_t index);
  bool HasQuery() const;

  raw_ptr<Browser> browser_ = nullptr;
  raw_ptr<Profile> profile_ = nullptr;
  SubmitCallback submit_callback_;
  base::RepeatingClosure close_callback_;
  std::unique_ptr<AutocompleteController> autocomplete_controller_;
  std::array<std::vector<Result>, kSectionCount> zero_state_results_;
  std::array<std::vector<Result>, kSectionCount> query_results_;
  std::array<std::vector<Result>, kSectionCount> displayed_results_;
  std::vector<TimedResult> open_tab_results_;
  std::vector<TimedResult> history_results_;
  std::vector<TimedResult> query_history_results_;
  std::vector<std::pair<size_t, size_t>> visible_results_;
  std::u16string user_input_;
  size_t selected_result_ = 0;
  OriginQuickOpenDisposition selected_disposition_ =
      OriginQuickOpenDisposition::kNewPage;
  raw_ptr<views::View> panel_ = nullptr;
  raw_ptr<views::ImageView> search_icon_ = nullptr;
  raw_ptr<views::Textfield> search_field_ = nullptr;
  raw_ptr<OriginQuickOpenTextButton> replace_button_ = nullptr;
  raw_ptr<OriginQuickOpenTextButton> split_button_ = nullptr;
  raw_ptr<OriginQuickOpenSpaceChip> current_space_chip_ = nullptr;
  raw_ptr<views::Label> send_to_space_label_ = nullptr;
  raw_ptr<views::View> send_to_space_container_ = nullptr;
  raw_ptr<views::View> shortcuts_footer_ = nullptr;
  std::vector<raw_ptr<OriginQuickOpenSpaceChip>> send_to_space_chips_;
  std::vector<std::string> send_to_space_ids_;
  std::array<raw_ptr<views::Label>, kSectionCount> section_labels_{};
  raw_ptr<views::Label> empty_state_label_ = nullptr;
  raw_ptr<OriginQuickOpenResultButton> command_row_ = nullptr;
  std::array<std::vector<raw_ptr<OriginQuickOpenResultButton>>, kSectionCount>
      section_rows_;
  std::optional<ui::KeyboardCode> activation_key_to_suppress_;
  bool updating_inline_autocomplete_ = false;
  bool suppress_inline_autocomplete_ = false;
  bool result_navigation_active_ = false;
  std::map<std::string, ui::ImageModel> favicon_models_;
  std::set<std::string> pending_favicon_hosts_;
  std::set<std::string> network_favicon_hosts_;
  base::CancelableTaskTracker task_tracker_;
  base::CancelableTaskTracker favicon_task_tracker_;
  base::WeakPtrFactory<OriginQuickOpenView> weak_factory_{this};
};

#endif // BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_QUICK_OPEN_VIEW_H_
