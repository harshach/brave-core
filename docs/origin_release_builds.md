# Origin Desktop Builds

Origin has separate GitHub Actions workflows for unsigned Linux, Windows, and
macOS developer packages:

- `origin-build-linux.yml` creates x64 Debian, RPM, and portable ZIP artifacts.
- `origin-build-windows.yml` creates an x64 installer and portable ZIP.
- `origin-build-macos.yml` creates a DMG and ZIP for ARM64 or Intel Macs.

Each workflow can be started manually. Pushing a tag matching `origin-v*` starts
Linux, Windows x64, and macOS ARM64 builds together. Artifacts are kept for 14
days in the workflow run.

## Runtime performance

Distributed Origin packages use Brave's true `Release` configuration with
`is_brave_release_build=1` and DCHECKs disabled. This checks out Chromium's PGO
profiles and enables the same PGO and ThinLTO optimization path used by Brave's
release binaries. Public builds use explicit non-secret placeholder values for
the service keys that Brave requires at GN generation time; those placeholders
do not grant access to paid Brave backend services. Shields' local blocking
engine is independent of those service credentials.

Do not distribute or performance-test the `Component` build:
it is designed for fast incremental linking, keeps DCHECKs enabled, and loads
hundreds of shared libraries. `Static` is appropriate for local functional
testing while a fully optimized `Release` package is being built, but it is not
the runtime-performance acceptance target.

## Build runners

Brave's Chromium checkout exceeds 60 GB before build output. Standard GitHub
hosted runners provide substantially less disk space, so the package workflows
use self-hosted runners. Provision runners according to Brave's platform build
guides and keep at least 250 GB of free SSD space for checkout and release
output.

Add the custom `origin-build` label in addition to the automatic runner labels:

| Platform    | Required labels                                 |
| ----------- | ----------------------------------------------- |
| Linux x64   | `self-hosted`, `origin-build`, `Linux`, `X64`   |
| Windows x64 | `self-hosted`, `origin-build`, `Windows`, `X64` |
| macOS ARM64 | `self-hosted`, `origin-build`, `macOS`, `ARM64` |
| macOS Intel | `self-hosted`, `origin-build`, `macOS`, `X64`   |

The runners need Git 2.41 or later, Python 3, current platform build tools, and
passwordless `sudo` on Linux so Chromium dependencies can be installed. The
workflows install the pinned Node.js and pnpm versions themselves. Keep the
Windows runner work directory close to the drive root and enable Developer Mode
to avoid path-length and symlink failures.

The Chromium checkout and Git object cache remain in the runner workspace to
make later builds incremental. Do not share one working directory between
concurrent runner processes.

## Signing

CI packages use `--skip_signing`. They are intended for development and local
testing, so Windows SmartScreen and macOS Gatekeeper can warn when they are
opened. Public distribution requires a separate signing and notarization stage
backed by protected GitHub environments and platform signing credentials.

## Manual dispatch

After these workflows are present on the repository's default branch, start them
from the Actions page or with GitHub CLI:

```sh
gh workflow run origin-build-linux.yml --ref master
gh workflow run origin-build-windows.yml --ref master
gh workflow run origin-build-macos.yml --ref master -f architecture=ARM64
```

Use `architecture=X64` for the Intel macOS package.
