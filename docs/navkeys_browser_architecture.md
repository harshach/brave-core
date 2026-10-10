# NavKeys Origin Architecture

NavKeys Origin is an Origin-branded desktop configuration centered on a native
tab tree and keyboard-driven workspaces. It reuses Brave and Chromium browser
primitives so Chromium security updates remain mergeable.

## Product invariants

- The left tab tree is the only visible tab strip in normal browser windows.
- New tabs with an opener become children of the opener.
- Closing a tree node can close its complete subtree; restoring a closed node
  restores its persisted hierarchy.
- Single-key commands run only when browser content is in navigation mode.
  Editable and interactive content receives unmodified keyboard input.
- Split view uses native `SplitTabCollection` and `BraveMultiContentsView`.
- The independent right panel uses `SidebarWebPanelController` and remains
  separate from split-tab state.
- Focus mode hides browser chrome without changing the underlying workspace.

## Upstream primitives

| Capability         | Existing implementation                           |
| ------------------ | ------------------------------------------------- |
| Hierarchical tabs  | `TreeTabNodeTabCollection` and `TreeTabModel`     |
| Tree persistence   | `TreeTabSessionManager`                           |
| Vertical UI        | `BraveVerticalTabStripRegionView`                 |
| Split tabs         | `SplitTabCollection` and `BraveMultiContentsView` |
| Web side pages     | `SidebarWebPanelController`                       |
| Focus presentation | Brave focus-mode frame views                      |

## Implemented behavior

- Origin always presents the native tree workspace on the left; stale profile
  settings cannot bring back a horizontal or icon-only tab strip.
- Space names, icons, and ordering are profile-persistent. Tab membership,
  selected space, and the last selected tab in each space are window-local and
  session-restored. An empty selected space restores without borrowing pages
  from another space.
- A page opened from another page is represented by Brave's native tree-tab
  parentage. Closing a branch and restoring it reuse the native historical-tab
  and tree-session data.
- Spaces sit in a 48 px row along the bottom of the sidebar, with 36 px
  controls. The sidebar starts at 306 px and resizes from 236 px to 416 px;
  hidden, it takes no width. The web canvas has an 8 px separation and 10 px
  corners.
- Chrome typography uses SF Pro Text on macOS, Segoe UI on Windows, and Inter on
  Linux. The sidebar holds the Space's pinned tiles and pages, with the Spaces
  row at the bottom; the Space title appears only while it is renamed.
- Resting on a Space's icon previews that Space's pages in the list without
  leaving the active Space; picking a page from the preview switches to its
  Space. `OriginSpaceController::SetPreviewSpace()` changes only what the list
  shows.
- Just above the Spaces row, short rows appear while they have something to
  say: what is playing in the window (play, pause, skip), a download in
  progress, a reminder that came due, and brief notices such as "Snoozed until
  Tomorrow 9:00 AM". With nothing to show they take no room
  (`OriginSidebarStrips`).
- Page rows: hovering turns the favicon into a done check that closes the page
  and its subpages as `d` does; `c` or a double-click renames the page in the
  list, which the page keeps across restarts; a pinned page remembers the
  address it was pinned at, `h` returns it there, Shift+H makes the current
  address its home, and its tile shows a dot while it is elsewhere.
- The top bar's address field and page actions follow the page's scroll:
  reading downward takes them away, coming back up returns them, as does
  Command/Ctrl+L. Hovering the bar holds it open but never summons it, because
  the page owns that strip while the bar is away and revealing on approach
  would move whatever the pointer was reaching for.
- Navigation mode maps the arrow keys and `j`/`k` to adjacent pages, `1`-`9` to the Space
  in that position of the bottom row, Command/Ctrl+Up and Command/Ctrl+Down to
  adjacent spaces, `o` and `n` to the native Quick Open palette, `d` to close
  the selected branch, `z` to restore, `r` to reload the active page, `s` to
  split the window and open a new page beside the current one, `p` to pin or
  unpin the page, `m` to mute or unmute whatever is making sound anywhere in
  the window, and `f` to focus mode. Space belongs to the page: sites use it
  to scroll and to pause video. Command/Ctrl+K opens Brave Commander
  and Command/Ctrl+Right closes the active split. Command/Ctrl+Backslash toggles
  the sidebar even when focus is outside the page; the macOS View menu exposes
  the same command. Command/Ctrl+Left also toggles the sidebar when a page has
  focus outside editable content. `i` enters edit mode and
  `Escape` returns to navigation mode. Editable page fields continue to
  receive their original input. `\` shows or hides the side view, `.` moves
  the page into it or back to the list, `?` opens the shortcut sheet, `c`
  renames, `h` returns a pinned page home, `l` snoozes, `a` sets a reminder,
  `t` tags, `w` closes the page, `e` edits the Space, and `[`/`]` go back and
  forward.
- A Space carries an equalizer badge only while an unmuted page in it is
  producing sound. Clicking the badge mutes the Space and hides it. Paused,
  silent and muted pages have no rail status. `OriginMediaMonitor` supplies
  the audible state; the badge paints itself rather than using a vector icon,
  which loses its shape at 16px.
- Quick Open classifies its input through Brave's native autocomplete stack.
  Replace and Split are persistent modes for the next result or `Enter`,
  selectable from the buttons beside the space chip or with Command/Ctrl+R and
  Command/Ctrl+S; Split creates a native split page before navigating its new
  pane, and submitting it with no input splits to a new page. Choosing a
  result that is already open while Split is armed pairs that existing page
  with the current one rather than loading a second copy beside itself. The
  shortcut
  legend stays visible so both modes are discoverable before typing. Empty
  input uses native zero-suggest history, and the footer enters Brave Commander.
- Split pages use Brave's native split-tab commands and resizable
  `BraveMultiContentsView`.
- The side view, after SigmaOS's, is a resizable column at the trailing edge
  of `BraveMultiContentsView` (280-760 px, 400 px by default, shared by the
  profile's windows). Its header switches between the Space's side page and
  tools: Shortcuts, Downloads, Reminders, Snoozed and Tags. A side page is a
  real tab shown by Brave's web panel: each Space keeps at most one, it is
  left out of the page list, page counts and `j`/`k`, follows its Space, and
  comes back with the tab after a restart. Fullscreen video in the page takes
  the window from the side view; fullscreen video in the side page takes it
  from the page. The downloads shortcut opens the Downloads tool.
- Snoozing (`l`) closes the page and reopens it in its Space, in the profile's
  most recent window, at the time typed in plain words ("in 2h", "fri 3pm")
  or picked from presets. A reminder (`a`) remembers the page and a time, and
  once due waits above the Spaces row until it is done. Both are kept in
  profile prefs by `OriginLaterService`; private windows have neither.
  Clicking a row in the Snoozed or Reminders tool opens its page.
- Tags (`t`) are bookmark folders under "Socket Tags", so a page can carry
  several and they sync like bookmarks. Typing `#tag words` in Quick Open
  lists the tagged pages that match.
- Brave Shields remains attached to each normal and split WebContents; neither
  spaces nor the right panel replace the network or content-blocking stack.

## Validation

The focused regression set covers Origin defaults, forced vertical-tree
presentation, Space CRUD and persistence, per-space tab isolation, last-page
selection, empty-space window restoration, fallback from invalid session data,
the right web-panel feature default, the side view and side pages, Space
preview, page rows (done, rename, pinned home), snoozing, reminders, tags and
the time parser. Chromium patch files are also checked
against the patched source before each handoff.

## Update policy

The customization branch merges upstream through reviewable sync pull requests.
Every sync must complete patch application, targeted unit and browser tests, and
a local Origin build before it is promoted to the daily browser. Chromium or
Brave changes that conflict with native UI patches are resolved in the sync
branch rather than hidden by an automatic force merge.
