# Security policy

## Supported versions

Only the latest release of Euclase Light receives security fixes.

## Reporting a vulnerability

Please do not publish exploit details in a public issue.

1. Use GitHub private vulnerability reporting: the **Security** tab of this repository, then **Report a vulnerability**.
2. If that option is not available, open an issue titled "Security contact request" without technical details, and a private channel will be arranged.

Expect an acknowledgement within a few days. This is a small project maintained by one person; there is no bug bounty.

## Scope

In scope: the OpenRGB client and downloader, the guarded bus layer, privilege elevation, settings handling and the release archive.

## Security-relevant design

- The application requests administrator rights only so that OpenRGB can reach RAM and motherboard buses through the PawnIO driver. GPU and USB devices work without elevation (`--no-elevate`).
- OpenRGB is downloaded from a pinned release URL and verified against a pinned SHA-256 before use. Archive extraction rejects paths that escape the target folder.
- The OpenRGB SDK connection is limited to `127.0.0.1:6742`.
- The guarded bus layer blocks writes to protected SPD address ranges (0x18-0x1F, 0x30-0x37, 0x50-0x5F) unless a write is explicitly granted. See `docs/SAFE_SCANNING.md`.
- No telemetry. The release executable is not code-signed yet; verify the SHA-256 published with each release.
- Third-party components (OpenRGB, PawnIO) are separate programs with their own security policies.
