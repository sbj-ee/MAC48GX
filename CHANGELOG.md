# Changelog

## 1.1.0 — signed in-app updates (macOS)

- **Check for Updates (Cmd+U) can now install.** When a newer GitHub release
  exists, the dialog offers **Install**, **Release Page** and **Later**.
  Install downloads `SHA256SUMS`, `SHA256SUMS.minisig` and
  `MAC48GX-<tag>.dmg`, checks the minisign signature against the public key
  compiled into the app (Ed25519 via vendored Monocypher 4.0.2), checks the
  .dmg's SHA-256 against the signed list, and only then opens the disk image
  for a drag-to-Applications install. A release that is unsigned, has a bad
  signature, or a mismatched checksum is refused and the download is deleted.
- **Check at startup toggle:** Cmd+Shift+U turns the launch-time check on or
  off (saved in `~/.droid48/check-updates-at-startup`; on by default).
- Release workflow writes `SHA256SUMS` for the .dmg, signs it with minisign
  from the `MINISIGN_SECRET_KEY` / `MINISIGN_PASSWORD` repo secrets, verifies
  it against the embedded key, and attaches both files to the release.
- New headless test suite `make test-update` (33 checks), run by `make test`
  on macOS and Linux CI.

## 1.0.3

- Previous release.
