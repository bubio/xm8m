# XM8M Privacy Policy

Effective date: 2026-08-24

XM8M is free, non-commercial, open-source software. It has no XM8M-operated
server, telemetry, analytics, advertising, in-app purchases, or paid features.

## RetroAchievements

RetroAchievements is optional and disabled unless the user enables RA mode.
When enabled, XM8M communicates directly with RetroAchievements and sends the
information required by its API: account name and login credential, game/media
hashes, achievement and leaderboard identifiers and results, Hardcore status,
unlock time/delay, Rich Presence/session information, and the XM8M User-Agent.
XM8M does not send ROM or D88 contents, local file paths, passwords after login,
or save-state contents. RetroAchievements independently controls information
received by its service; consult its published privacy terms and account tools.
Server-location disclosure is not applicable to XM8M because XM8M operates no
server or data warehouse. RetroAchievements operates its own infrastructure;
XM8M's maintainers do not know or control its server locations and make no
representation about them.

## Local data and retention

XM8M stores its settings and RA data in the platform's application-data area.
The RA SQLite database contains library metadata, media hashes, achievement
progress, settings, image-cache metadata, and pending unlocks. Pending unlocks
contain the account name, achievement ID, Hardcore flag, game hash, unlock time,
state, and retry information—never an API token or raw POST body. They remain
until successfully synchronized, or until the user confirms Logout/RA-data
deletion. The API token remains in the operating system credential store until
Logout. Badge images use a 128 MiB least-recently-used cache. Library records,
app-managed D88 working copies, and states remain until the user deletes them.

Logout removes the credential. If pending unlocks exist, XM8M asks for
confirmation; confirmed Logout also deletes those pending records. Removing the
application's data removes its local RA database, cache, working copies, states,
and credentials according to the operating system's behavior. Android excludes
the RA directory and credentials from cloud backup and device transfer.

## Data responsibility and requests

XM8M's maintainers do not receive or control personal data through an XM8M
server because no such server exists. The user controls data kept locally on the
device. RetroAchievements is the data controller for personal data submitted to
its service, including GDPR access/deletion requests concerning that service.

Policy source: https://github.com/bubio/xm8m/blob/main/PRIVACY.md
