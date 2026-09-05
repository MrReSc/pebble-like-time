---
name: pebble-release
description: Release and publish this Pebble project, including versioning, deterministic Emery screenshots, Git commit/tag/push, and Pebble Appstore upload. Use when preparing or publishing a new version of Pebble Like Time; do not use for ordinary development builds.
---

# Pebble Like Time Release

Prepare releases reproducibly and leave the repository, Git remote, artifacts,
and Pebble Appstore entry on the same version.

Before releasing, confirm the requested version and which external operations
are authorized. A release request does not implicitly authorize Git push, live
Appstore publication, or replacement of existing Appstore screenshots.

Use the repository's `pebble-watchface` skill for build, QEMU, logging, and
visual-verification requirements. For the project-specific versioning,
screenshot fixtures, Git sequence, publishing command, and failure handling,
read [references/release-process.md](references/release-process.md) completely.

Do not modify the `pebble-watchface-agent-skill` submodule as part of a product
release. Do not create the commit or tag until the PBW and screenshots have
passed verification.
