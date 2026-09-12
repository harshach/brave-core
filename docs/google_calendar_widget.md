# Google Calendar widget

The widget shows one local day from the connected account's primary Google
Calendar. Previous/next buttons move by calendar date; Today returns to the
current day and follows midnight. Earlier events remain visible. Recurring
instances, all-day events, overnight events, locations, and meeting links come
from the Google Calendar API. The agenda uses a prominent date heading, compact
all-day cards, and timed entries with a dot, time, title, and location. Long
titles wrap to two lines. It shows only the selected day's agenda, without a
month grid. Clicking an event opens its full details.

Events refresh every minute while the widget is visible, immediately on day
navigation or reopening, and through Refresh. Transient failures retry after
three minutes and retain the last successful result for that day. Calendar data
is held in memory. Private windows do not inherit the connection.

## Configure the application

Create a Google Cloud OAuth client of type **Desktop app**, enable the Calendar
API, and configure the consent screen for the read-only
`https://www.googleapis.com/auth/calendar.events.readonly` scope. For a client
in testing mode, add the Google accounts that will test it as test users.
Download that desktop client JSON and keep it out of source control.

Place the JSON at `<user-data-dir>/google-calendar-oauth.json`, or launch with:

```
--origin-google-calendar-oauth-config=/absolute/path/desktop-client.json
```

For this workspace's manual profile, the default path is
`.context/manual-panel-test-profile/google-calendar-oauth.json`.
The file uses Google's downloaded `installed` object with `client_id` and
`client_secret`. The implementation fixes Google's authorization/token
endpoints; arbitrary endpoints in the JSON are not used. The client JSON
identifies the desktop application and is never requested from end users by
the widget. A distributable build needs application-owned OAuth configuration
and Google's applicable consent-screen verification.

Choose **Connect Google Calendar** and grant calendar read access in the
regular browser tab. Authorization uses a fresh state nonce, an S256 PKCE
challenge, and a temporary listener bound only to 127.0.0.1 on an ephemeral
port. It expires after five minutes or can be cancelled from the widget.
The refresh token is encrypted using the browser's OS encryption service and
stored as a non-synced profile preference. Access tokens stay in memory.
Disconnect clears local credentials and requests token revocation.

## Manual checks

1. Connect a Google account, then confirm today's timed, all-day, recurring,
   and already-finished events appear.
2. Move forward/back a day, cross a month boundary, and return with Today.
   Quickly changing days must not show a late result for the previous day.
3. Edit an event in Google Calendar; leave the widget visible and confirm it
   updates on its next refresh. Close/reopen the panel for an immediate refresh.
4. Go offline and refresh: an error must appear while prior results stay visible.
   A successfully loaded empty day must say "No events for this day".
5. Disconnect in one window; other windows must clear their calendar events.
   Restart after reconnecting and confirm the connection is restored.

Focused native tests: `OriginCalendarSourceTest.*`,
`OriginCalendarServiceTest.*`, and `OriginMediaMonitorTest.*` in
`brave_unit_tests`.

References: [Google desktop OAuth](https://developers.google.com/identity/protocols/oauth2/native-app),
[Calendar event queries](https://developers.google.com/workspace/calendar/api/v3/reference/events/list),
[Calendar access scopes](https://developers.google.com/workspace/calendar/api/auth).
