# Playlist file format

Phase 10 supports local `.m3u` and `.m3u8` files through the reusable
`loadNeniaM3uPlaylist()` and `saveNeniaM3u8Playlist()` helpers.

## Loading

- Text is interpreted as UTF-8 for both `.m3u` and `.m3u8`.
- Blank lines and lines beginning with `#` are ignored, including `#EXTM3U`.
- Relative entries resolve against the playlist file's parent directory.
- Absolute local filesystem paths are accepted.
- Remote HTTP, HTTPS, PLS, and XSPF entries are not supported.
- Missing, unreadable, non-audio, image, JSON, MIDI, and nested-playlist
  entries are skipped with a concise warning.
- Loading fails transactionally if no valid audio remains; the previous
  in-memory playlist is retained by the controller.
- Authored order is preserved exactly, including intentional duplicates.

The parser returns valid `NeniaPlaylistItem` values plus warning strings. The
standalone host reports warnings without opening a modal dialog.

## Saving

The save format is UTF-8 M3U8:

```text
#EXTM3U
relative/or/absolute/local/path.mp3
```

No `#EXTINF` metadata is required. Local paths are written relative to the
target playlist directory whenever possible; absolute paths are a valid
fallback. The in-memory item order and duplicates are preserved. A save only
writes the playlist file; it never modifies audio, artwork, metadata, or MIDI
sidecars.

`QSaveFile` provides atomic replacement. A failed write or commit returns an
error and leaves an existing target intact. The standalone S shortcut saves
back to the current loaded M3U/M3U8 target. Folder-imported or otherwise
untargeted lists open a Save dialog and default to `.m3u8`; the dialog is not
silently bypassed.

Playlist-file imports do not update the app's `playlist/lastFolder` setting.
That setting is reserved for the most recently successful folder import.
