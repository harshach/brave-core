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
- The workspace rail is 56 px, space controls are 32 px, the complete panel
  starts at 240 px and resizes from 220 px to 420 px. The web canvas has an 8 px
  separation and 10 px corners.
- Chrome typography uses SF Pro Text on macOS, Segoe UI on Windows, and Inter on
  Linux. The sidebar contains only the selected space title and its pages; the
  duplicate footer-level New Page control is suppressed in Origin.
- Navigation mode maps the arrow keys (with `j` and `k` aliases) to adjacent
  pages, Command/Ctrl+Up and Command/Ctrl+Down to adjacent spaces, Space and
  `o` to the native Quick Open palette, `d` to close the selected branch, `z`
  to restore, `r` to reload the active page, and `f` to focus mode.
  Command/Ctrl+K opens Brave Commander and Command/Ctrl+Right closes the active
  split. `m` mutes or unmutes every page in the selected space, `w` shows or
  hides the widgets panel, `i` enters edit mode, and `Escape` returns to
  navigation mode. Editable page fields continue to receive their original
  input.
- Quick Open classifies its input through Brave's native autocomplete stack.
  Replace and Split Screen are persistent modes for the next result or `Enter`;
  the latter creates a native split page before navigating its new pane. Empty
  input uses native zero-suggest history, and the footer enters Brave Commander.
- Split pages use Brave's native split-tab commands and resizable
  `BraveMultiContentsView`. A page added to the sidebar opens as an independent,
  resizable right web panel, suitable for Spotify, YouTube, or another web app.
- Brave Shields remains attached to each normal and split WebContents; neither
  spaces nor the right panel replace the network or content-blocking stack.
- A space carries an equalizer badge only while an unmuted page is producing
  sound. Clicking it mutes the space and hides the badge. Paused, silent and
  muted pages have no rail status; media widgets and tab controls can unmute
  them. `OriginMediaMonitor` supplies both the audible state and widget media
  metadata.
- The widgets panel is a fixed 280 px column on the trailing edge, mirroring
  the spaces panel. It reserves its width in
  `BraveBrowserViewTabbedLayoutImpl` before the sidebar is placed, so the two
  never overlap. Both panels use the same surface color, platform font,
  58 px header and 66 px content offset. Its open state is a profile preference
  observed by all windows.

## Widgets

Widgets are native views listed in a catalog
(`browser/ui/views/frame/origin_widgets/origin_widget_registry.cc`). Adding one
means subclassing `OriginWidgetView`, giving it a static factory, and appending
a descriptor; the panel, the add-widget picker, and the persisted per-profile
ordering are all driven from the catalog. Defaults apply only to an unset
preference; removing every widget leaves an empty panel.

The shipped widgets are:

- **YouTube** — shows a live video preview when its source page is in the
  background. The card hides when the video page is visible, including split
  view. A local Viz capture stream crops the video region; returning to the
  source page or hiding the panel releases the capture. Playback stays in the
  original tab, so there is one player and one audio stream.
- **Spotify** — follows the web player's media session across Spaces.
  Play/pause uses Chromium's Resume/Suspend controls, including the fallback
  for pages without custom action handlers. The web player requires a working
  Widevine component in the browser profile.
- **Google Calendar** — a read-only primary-calendar agenda for one local day.
  Previous/next and Today controls select the day; the view includes earlier
  events, all-day events, and recurring instances expanded by Google. It
  refreshes every minute while visible and retries transient failures after
  three minutes. Day navigation cancels stale requests, pagination is complete
  before publishing results, and failures preserve the last successful result
  for the selected day. Today follows midnight using local calendar arithmetic.

Google Calendar uses a profile-scoped OAuth connection with PKCE and an
expiring loopback listener. Sign-in opens a regular browser tab. Refresh tokens
are encrypted with OSCrypt and are not synced; access tokens and event data
stay in memory. Disconnect clears local credentials and requests revocation.
See [Google Calendar setup](google_calendar_widget.md) for desktop client
configuration and focused validation.

## Validation

The focused regression set covers Origin defaults, forced vertical-tree
presentation, Space CRUD and persistence, per-space tab isolation, last-page
selection, empty-space window restoration, fallback from invalid session data,
and the right web-panel feature default. Google Calendar day boundaries, event parsing, pagination, token refresh,
and media playback controls are covered by focused unit tests. Chromium patch files are also checked against
the patched source before each handoff.

## Update policy

The customization branch merges upstream through reviewable sync pull requests.
Every sync must complete patch application, targeted unit and browser tests, and
a local Origin build before it is promoted to the daily browser. Chromium or
Brave changes that conflict with native UI patches are resolved in the sync
branch rather than hidden by an automatic force merge.
