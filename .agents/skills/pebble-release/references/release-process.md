# Pebble Like Time release process

Follow this sequence for a release of this repository. Substitute the approved
version and release notes where shown.

## 1. Preconditions

1. Confirm the current branch, worktree changes, remote, latest release tag,
   and that the target version and tag do not already exist.
2. Preserve unrelated user changes. Stop if they overlap release files or make
   the release contents ambiguous.
3. Confirm explicit authorization before Git push, live Appstore publication,
   or `--replace-screenshots`. These operations are independent permissions.
4. Keep the application UUID and the `emery` target unchanged for an ordinary
   release.

## 2. Prepare the release

1. Update `package.json` to the approved semantic version.
2. Add concise English release notes to `STORE_LISTING.md`, retaining earlier
   release notes as history.
3. Make the approved product changes without editing the external skill
   submodule.
4. Review the diff and validate JSON before building.

## 3. Build and verify in Emery

1. Run `pebble build` and verify that `build/pebble-like-time.pbw` exists and
   contains the intended version and UUID.
2. Start buffered log capture before installation, install on a clean Emery
   emulator when stale state is possible, and relaunch the watchface if the
   emulator remains in the launcher.
3. Exercise release-specific states. For battery presentation changes, inspect
   `0`, `73`, and `100` percent. Check cropping, positioning, colors, contrast,
   status-row visibility, and logs after every material visual change.
4. Stop the background log process when verification is complete.

## 4. Capture deterministic Store screenshots

Use these fixtures for both Store screenshots:

- battery: `73%`
- steps: `12345`
- heart rate: `72 bpm`
- sunrise: `401` minutes (`06:41`)
- sunset: `1207` minutes (`20:07`)
- Timeline Quick View: off
- clock time: `06:07`
- sun date: the emulator's current local date as `YYYYMMDD`

After installation, set fixtures explicitly with the emulator commands rather
than relying on persisted state. Send sun values with message keys `10002`,
`10003`, and `10004` to application UUID
`11f623c8-eabe-48b3-bbe1-67ec92db7d11`.

On Emery, inject the `72 bpm` reading several times about one second apart,
after any clock adjustment. A single raw reading may not yet be exposed through
`HealthMetricHeartRateBPM`. Screenshot capture can also return a transitional
frame immediately after a redraw; capture again and keep only a visually
complete frame.

Capture the 24-hour image after setting the emulator to 24-hour format as
`store-assets/emery_screenshot_24h.png`. Then switch to 12-hour format, keep all
fixtures unchanged, and capture `store-assets/emery_screenshot_12h.png`.

Open both images at original detail. Verify that the heart icon and `72` appear,
all required rows are visible, the time format differs as intended, the visual
change is present, and no content is clipped. Do not regenerate Store icons for
a watchface-only visual change unless the release explicitly changes them.

## 5. Commit, tag, and push

1. Review the final diff, confirm that only intended tracked files changed, and
   run the final build/checks.
2. Commit the complete release, including updated tracked screenshots.
3. Create an annotated tag named exactly as the package version, matching this
   repository's existing tag convention (no `v` prefix).
4. If authorized, push the branch first and then the exact tag. Verify both
   remote refs before publishing.

Never tag an unverified build. Never move or overwrite an existing release tag.

## 6. Publish to the Pebble Appstore

1. Check `pebble login --status`. If credentials are absent or cannot refresh,
   run `pebble login` and complete the browser flow before continuing.
2. For the existing app, run `pebble publish --non-interactive` with the exact
   release notes and approved visibility. Add `--is-published` only when live
   publication is authorized.
3. Use `--no-gif-all-platforms` for this static Emery-only watchface. When
   screenshot replacement is authorized, pass both tracked Emery screenshots
   with `--screenshots` and add `--replace-screenshots`.
4. Confirm from the CLI response that the existing UUID was resolved, the PBW
   version matches, the upload succeeded, and the release is visible when live
   publication was requested.

Screenshot replacement is irreversible through the CLI. Do not add that flag
by default and do not retry an ambiguous failed request until the Store state
has been checked.

## 7. Completion checks

Verify the local worktree is clean, the remote branch and annotated tag resolve
to the release commit, and the Store reports the intended version. Report the
PBW path, commit, tag, push result, Store result, and verified screenshots. If
publication fails after Git push, preserve the immutable Git release and report
the Store failure precisely; retry only when it is clear that no release was
created or a safe idempotent retry is supported.
