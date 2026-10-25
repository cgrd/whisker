# Security policy

## Supported versions

Only the latest release gets security fixes. Update with the install one-liner in the [README](README.md).

## Reporting a vulnerability

Report it privately through GitHub: open the repo's **Security** tab, then **Report a vulnerability** (or go straight to https://github.com/cgrd/whisker/security/advisories/new). Don't open a public issue.

Include what you found, how to reproduce it and the Whisker version (`Whisker-x86_64.AppImage --version`).

What to expect:

* An acknowledgement within 7 days
* An initial assessment within 14 days
* A fix and a published advisory as soon as one is ready, with credit to you unless you'd rather stay anonymous

## Scope

Whisker reads input devices (hidraw, evdev), creates a virtual keyboard through uinput, listens on a Unix socket in `$XDG_RUNTIME_DIR` and runs commands you configure for pedals. Bugs that let another user or a remote party trigger typing, run commands or read audio are in scope. So are problems in the install script, release artifacts and the Decky plugin, which runs as root.

The speech model is downloaded from Hugging Face and isn't covered by this policy.

## Verifying downloads

Every release has a `SHA256SUMS` file, and the release assets carry GitHub build provenance:

```sh
sha256sum -c --ignore-missing SHA256SUMS
gh attestation verify Whisker-x86_64.AppImage.zip --repo cgrd/whisker
```
