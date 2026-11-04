# Automatic song catalog — parked prototype

**Requested November 4, 2026: prepare this for later; do not activate it.**

To resume, tell Codex: **"Activate the automatic song catalog using SONG_CATALOG_PLAN.md."**

## Current status

- The active `firmware/Soundpod_ESP32/Soundpod_ESP32.ino` is unchanged by this feature.
- No files on the SD card have been added, renamed, moved, or deleted.
- Nothing has been uploaded to the ESP32, and no serial port has been opened.
- A working, offline catalog builder is in `../extras/song_catalog/catalog_tool.py`.
- USB catalog transfer, ESP32 persistent storage, and playback integration are **designed below but not implemented or enabled**. This is not yet a complete synchronization system.
- The existing firmware still uses its ten hardcoded song entries, durations, and startup jingle at `/mp3/0011.mp3`.

## Intended experience after activation

1. Put numbered, named MP3s into a local staging folder on the Mac.
2. Run the future Sync Songs helper. It validates all files before changing anything, reads durations from the actual MP3s, and previews the ordered playlist.
3. The helper prepares the DFPlayer-compatible names on the SD card and transfers the matching catalog to ESP32 internal flash over USB.
4. Safely eject the card, power the player off, insert the card into the DFPlayer, and restart. No per-song firmware edits or reuploads are needed.

There are two separate destinations: audio goes onto the DFPlayer's SD card; metadata goes into ESP32 internal storage. The ESP32 cannot read an extra `catalog.json` placed on the DFPlayer's card. Changing the card requires another catalog synchronization.

## Naming and ordering

Accepted source filenames:

```text
1-Ethel_Cain-Janie.mp3
2-Ethel_Cain-Willoughby's_Theme.mp3
10-Ethel_Cain-Waco,_Texas.mp3
12-Another_Artist-Another_Song.mp3
```

- Sort by the numeric prefix, so `2` comes before `10`.
- Replace underscores with spaces in artist and title.
- Split at the first two hyphens. Further hyphens belong to the title. Artist names containing hyphens need a later escaping rule or ID3 metadata support.
- Read duration from the MP3 audio stream using Mutagen; no duration in the filename is needed. Variable-bitrate durations depend on the validity of the file's headers and should be checked on representative files.
- The current prototype uses filenames for artist/title. Reading ID3 artist/title tags is an optional later extension, not implemented here.
- Reject duplicate song numbers, missing artist/title, invalid MP3s, and empty catalogs before committing anything.
- Initial prototype limit: 200 songs, with artist and title each limited to 120 UTF-8 bytes. These are implementation limits, not claims about DFPlayer capacity. Confirm memory use before increasing them.
- JSON preserves Unicode, but the current OLED fonts may not render every character. Decide on a tested display transliteration policy when activating.

The DFPlayer playback copy needs four-digit prefixes, for example:

```text
/mp3/0001-Ethel_Cain-Janie.mp3
/mp3/0002-Ethel_Cain-Willoughby's_Theme.mp3
```

The offline builder only reports these destination names; **it does not rename or copy songs**. A source named `1-...mp3` must not simply be copied unchanged for the current `playMp3Folder()` scheme.

### Startup sound reservation

**Song number 11 is reserved.** `/mp3/0011.mp3` remains the startup sound and must not appear in the regular playlist. The builder ignores this exact plain-number jingle filename and rejects a named song with ID 11. Use ID 12 after ID 10 for now. Other plain-number files such as `0001.mp3` require descriptive names before catalog generation.

Playlist positions and DFPlayer file numbers must become separate values. With IDs `1, 2, 10, 12`, Next after ID 10 must select ID 12. Do not use `currentTrack++` as the playback ID. If continuous numbering is preferred later, explicitly migrate the jingle to another reserved number in both firmware and SD card together, with collision checks.

## Try the offline builder later

These are optional preparation commands, **not activation instructions for the current firmware**. Python 3 is required. Run from this project's root:

```sh
python3 -m venv /tmp/panda-song-catalog-venv
/tmp/panda-song-catalog-venv/bin/python -m pip install -r extras/song_catalog/requirements.txt
/tmp/panda-song-catalog-venv/bin/python extras/song_catalog/catalog_tool.py "/path/to/local/music-folder"
```

By default the catalog is printed. To save a new JSON file locally, add:

```text
--output "/path/to/new-local-catalog.json"
```

The builder refuses to overwrite existing output or write inside the input music folder or `/Volumes`. It only scans immediate MP3 files, ignores hidden files, and never connects to a device. Host tests:

```sh
python3 -m unittest discover -s extras/song_catalog -p 'test_*.py'
```

Catalog schema version 1:

```json
{
  "schema_version": 1,
  "reserved_intro_id": 11,
  "tracks": [
    {
      "id": 1,
      "artist": "Ethel Cain",
      "title": "Janie",
      "duration_ms": 300000,
      "source_filename": "1-Ethel_Cain-Janie.mp3",
      "playback_filename": "0001-Ethel_Cain-Janie.mp3"
    }
  ]
}
```

The illustrated duration is an example; the helper measures the actual file.

## Implementation checklist when Michael asks to activate

1. Back up the active sketch and inspect the connected board, serial port, SD files, and current playlist. Confirm current pin assignments from `WIRING.md`.
2. Keep the catalog tool in its own folder; add a preview-and-confirm SD preparation step with no silent deletion of unrelated files. Validate destination capacity, duplicate numeric prefixes, and jingle conflicts. Ignore/clean macOS `._` resource files deliberately before card use.
3. Add an explicit USB catalog import mode to firmware. Do not treat ordinary Serial Monitor traffic as catalog data. The existing UART2 on GPIO16/17 stays dedicated to DFPlayer; catalog transfer uses the USB programming serial interface.
4. Implement a bounded transfer protocol, for example `PANDA_CATALOG_V1 <byte_count> <sha256>`, READY, then exactly that many JSON bytes, then a validated OK response. Limit input size, enforce timeouts, and reject invalid JSON/schema, out-of-range IDs, duplicate/reserved IDs, invalid durations, or excess strings/tracks. Account for ESP32 resets when opening a serial port; only transmit after its explicit ready handshake.
5. Store the validated catalog atomically in internal flash (for example LittleFS with a temporary file and a recoverable replacement strategy). Preserve the last valid catalog if transfer, power, validation, or storage fails. Do not rewrite flash on each boot or playback change. Read back/hash the committed catalog before reporting sync success.
6. Replace fixed tables with a catalog abstraction that supplies artist, title, duration, and **DFPlayer ID** for each playlist position. Route startup, Play, Next, Previous, automatic advance, wraparound, and OLED rendering through it. Preserve existing automatic-next duplicate-event protection and responsive button handling.
7. Preserve the current hardcoded catalog as a fallback until a valid imported catalog exists. Keep the jingle outside the playlist. Reconcile retained deep-sleep state when the catalog changes (save a catalog identity with track state, or validate/reset the selection).
8. Use measured duration for total time/progress; retain the pause-aware ESP32 elapsed timer. DFPlayer provides no reliable actual playback-position query here, so elapsed time remains an estimate and can differ slightly from audio startup timing. Advance only on the existing playback-finished event, never solely on the metadata duration.
9. Compile and run meaningful host tests for sorting, parsing, storage failures and malformed/interrupted transfers; verify actual upload with the connected board. Physically test a catalog with gaps, more than ten songs, a hyphenated title, variable-bitrate duration, pause/resume, wraparound, startup jingle exclusion, and sleep/wake. Synchronize a second changed catalog and power-cycle to verify persistence and state reconciliation.
10. Update the build guide and activation status after readback and physical confirmation. Report any untested hardware behavior separately.

Before connecting ESP32 USB for eventual sync, disconnect the boosted 5 V feed using the documented programming link. Re-establish battery power only after unplugging USB. No extra components are required for the catalog workflow.

## References and limitations

### Prototype verification (November 4, 2026)

- Six host tests passed: name parsing, invalid names/reserved number, numeric ordering/hidden-file exclusion, duplicate rejection, invalid duration, and safe output creation.
- A temporary copy of the actual startup MP3 measured 5,851 ms with Mutagen 1.48.1; it was treated as a sample named song solely for this offline test. The production jingle and SD card were untouched.
- An end-to-end CLI check confirmed the source MP3 hash was unchanged, existing JSON output was not overwritten, and corrupt MP3 input failed with an error.
- Active firmware SHA-256 remained `e982c836e0f05b413c10b878c29449d046491942484754549240054e59fdcb70` before and after preparation.
- No USB transfer, firmware compilation, upload, or physical playback test was performed for this inactive feature.

- DFRobot serial API exposes counts/state/file numbers, not filenames, ID3 or durations: <https://github.com/DFRobot/DFRobotDFPlayerMini/blob/master/DFRobotDFPlayerMini.h>
- Four-digit `/mp3/` naming with descriptive suffixes: <https://wiki.dfrobot.com/dfr0299/docs/20906>
- Active sketch: `firmware/Soundpod_ESP32/Soundpod_ESP32.ino`.
- This prototype does not add direct ESP32 access to the DFPlayer SD card. A future ESP32-readable card/audio redesign is a separate hardware project.
