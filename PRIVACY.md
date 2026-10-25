# Privacy Notes

This note describes behavior found in Nenia's source code; it is not a legal
certification or a guarantee about external libraries, operating-system
services, or user-provided media.

## Application behavior

The audited application code contains no Nenia-owned telemetry, analytics,
account system, crash-report upload, automatic update service, or media-upload
path. The standalone host's normal file and folder import paths are local.
The optional KDE/XDG file chooser uses the local desktop portal over DBus.

Nenia's reusable playback API accepts a QUrl, and Qt Multimedia may access
local files or network resources for a source supplied by its caller. The
project therefore does not claim that network access is technically
impossible. Qt plugins, codecs, desktop portals and other system components
have their own behavior and policies.

## Local settings and files

Nenia uses Qt QSettings for preferences such as the selected color scheme,
playback mode, keyboard display options, last-opened folder and selected
SoundFont path. These settings can therefore contain local paths. Playlist
contents are saved only when the user explicitly chooses Save Playlist; Nenia
does not automatically upload or export a playlist.

Playback metadata and artwork are used for presentation. The application does
not add an online artwork-fetch service. User-selected audio, playlists,
artwork and SoundFonts remain local inputs unless a caller deliberately
supplies a remote URL through the reusable API.

## Logs

Warnings and diagnostics may include an absolute media or SoundFont path,
especially when metadata loading fails or SoundFont configuration is
reported. Review and redact logs before sharing them publicly.

For dependency-specific privacy and network behavior, consult the Qt,
codec/backend and operating-system documentation for the packages installed
on the target machine.
