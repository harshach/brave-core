// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_ADDRESS_PILL_H_
#define BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_ADDRESS_PILL_H_

#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/view.h"

class BraveShieldsToolbarButton;
class Browser;

namespace views {
class Button;
class ImageButton;
class MenuRunner;
}  // namespace views

// The page's address at the top of Origin's sidebar, where Arc keeps it. The
// top bar stays away, so this is where the address is read: clicking the host
// edits it in Quick Open, and the buttons beside it reach extensions, Copy
// Link, Shields and a menu with the page's site settings and pin actions.
class OriginAddressPill : public views::View,
                          public ui::SimpleMenuModel::Delegate,
                          public views::ContextMenuController {
  METADATA_HEADER(OriginAddressPill, views::View)

 public:
  enum Command {
    kCopyLink = 1,
    kCopyLinkAsMarkdown,
    kCopyLinkToHighlight,
    kSiteSettings,
    kExtensions,
    kPinPage,
    kBackToPinnedAddress,
    kReplacePinnedAddress,
    kEditPinnedAddress,
    kSeparateFromPin,
    kUnloadKeepPinned,
  };

  struct Callbacks {
    Callbacks();
    Callbacks(Callbacks&&);
    Callbacks& operator=(Callbacks&&);
    ~Callbacks();

    base::RepeatingClosure edit_address;
    // Runs the commands the browser view owns: copying and pin actions.
    base::RepeatingCallback<void(Command)> run_command;
    base::RepeatingCallback<bool(Command)> is_command_enabled;
  };

  OriginAddressPill(Browser* browser, Callbacks callbacks);
  OriginAddressPill(const OriginAddressPill&) = delete;
  OriginAddressPill& operator=(const OriginAddressPill&) = delete;
  ~OriginAddressPill() override;

  // Shows the active page's address.
  void Update();

  // The site settings and extensions bubbles, under the pill.
  void ShowSiteSettings();
  void ShowExtensions();

  const std::u16string& GetHostTextForTesting() const;
  views::Button* host_button_for_testing();
  views::Button* copy_button_for_testing();
  views::Button* extensions_button_for_testing();
  views::Button* menu_button_for_testing();
  std::unique_ptr<ui::SimpleMenuModel> BuildMenuModelForTesting();

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;
  void OnThemeChanged() override;
  void OnPaintBackground(gfx::Canvas* canvas) override;

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdEnabled(int command_id) const override;
  bool GetAcceleratorForCommandId(int command_id,
                                  ui::Accelerator* accelerator) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override;

 private:
  class HostButton;

  void UpdateColors();
  std::unique_ptr<ui::SimpleMenuModel> BuildMenuModel();
  std::unique_ptr<ui::SimpleMenuModel> BuildCopyMenuModel();
  void RunMenu(std::unique_ptr<ui::SimpleMenuModel> model,
               views::View* anchor,
               ui::mojom::MenuSourceType source_type);
  void OnHostPressed();
  void OnMenuButtonPressed();

  const raw_ptr<Browser> browser_;
  Callbacks callbacks_;
  bool has_page_ = false;

  raw_ptr<HostButton> host_button_ = nullptr;
  raw_ptr<views::ImageButton> extensions_button_ = nullptr;
  raw_ptr<views::ImageButton> copy_button_ = nullptr;
  raw_ptr<BraveShieldsToolbarButton> shields_button_ = nullptr;
  raw_ptr<views::ImageButton> menu_button_ = nullptr;

  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
};

#endif  // BRAVE_BROWSER_UI_VIEWS_FRAME_ORIGIN_ADDRESS_PILL_H_
