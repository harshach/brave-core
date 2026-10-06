# Socket macOS releases

Socket macOS releases are universal DMGs built from the long-lived feature
branch. The release workflow builds Intel and Apple Silicon applications on an
Apple Silicon Mac, combines them, signs the result with Developer ID, submits
it to Apple's notarization service, and uploads the verified DMG to GitHub
Releases. It also publishes matching framework dSYMs for both architectures
and verifies their UUIDs against the distributed universal binary.

## Build host

Register an Apple Silicon Mac as a repository-level self-hosted GitHub Actions
runner and add the custom `socket-build` label. Put the runner work directory
on an SSD with at least 250 GiB free; 400 GiB or more is recommended for the
two Chromium output trees and persistent caches.

GitHub-hosted macOS runners have 14 GiB of SSD storage, including the larger
macOS runner sizes, so they cannot hold this checkout and build output.

## Apple credentials

The `socket-release` GitHub environment needs these secrets:

| Secret | Value |
| --- | --- |
| `APPLE_ID` | Apple ID used for notarization |
| `APPLE_TEAM_ID` | Ten-character Apple Developer Team ID |
| `APPLE_APP_SPECIFIC_PASSWORD` | App-specific password for `notarytool` |
| `MACOS_CERTIFICATE_P12` | Base64-encoded Developer ID Application certificate and private key |
| `MACOS_CERTIFICATE_PASSWORD` | Password protecting the P12 file |

Create a Developer ID Application certificate in the Apple Developer portal,
install it with its private key, and export both as a password-protected P12.
Socket does not package a PKG, so a Developer ID Installer certificate is not
required. The build removes Brave-only restricted entitlements and does not
embed Brave's provisioning profile.

Create the environment and secrets with GitHub CLI:

```sh
gh api --method PUT repos/harshach/brave-core/environments/socket-release
gh secret set APPLE_ID --env socket-release
gh secret set APPLE_TEAM_ID --env socket-release
gh secret set APPLE_APP_SPECIFIC_PASSWORD --env socket-release
openssl base64 -A -in DeveloperIDApplication.p12 | \
  gh secret set MACOS_CERTIFICATE_P12 --env socket-release
gh secret set MACOS_CERTIFICATE_PASSWORD --env socket-release
```

The workflow imports the P12 and stores the notarization credentials in a
temporary keychain. Only the temporary profile name is written to the build
arguments. An `always()` cleanup step removes the keychain, P12, and local
build configuration; the Chromium checkout and compiler caches remain so later
releases are incremental.

## Publish

The workflow lives on the feature branch rather than the repository's default
branch. Publish by pushing a release tag that points to the desired feature
commit:

```sh
git tag -a socket-v0.1.0-preview.1 -m "Socket 0.1.0 preview 1"
git push origin socket-v0.1.0-preview.1
```

Tags with a suffix such as `-preview.1` create prereleases. Tags without a
suffix create regular releases. The workflow verifies the bundle identifier,
both CPU architectures, dSYM UUIDs, code signature, notarization ticket, and
DMG before it creates or updates the GitHub Release.
