# Updates

Whisker updates itself from GitHub releases, and only installs a release whose manifest is signed by a key compiled into the running binary.

## For users

The installer sets up `whisker-update.timer`, a systemd user timer that runs `Whisker-x86_64.AppImage --auto-update` once a day at a random time. It does nothing while `auto_update: false` is in `~/.config/pedal-stt/config.yaml` (also in the settings window and the Decky panel). Commands:

```sh
~/Applications/Whisker-x86_64.AppImage --check-update   # report only
~/Applications/Whisker-x86_64.AppImage --update         # install now
systemctl --user list-timers whisker-update.timer       # next run
journalctl --user -u whisker-update                     # what the last run did
```

An update:

1. Asks GitHub which tag `/releases/latest` points to. Same or older than the running version: done.
2. Downloads `whisker-manifest.json` and `whisker-manifest.json.minisig` from that tag over HTTPS, with size limits.
3. Checks the minisign signature and its trusted comment against the public keys built into Whisker, and refuses a manifest whose version differs from the tag or isn't newer than the running version, so an old release can't be replayed to downgrade anyone.
4. Asks the daemon over its command socket whether it's recording. If it is, the timer run gives up until tomorrow.
5. Downloads `Whisker-x86_64.AppImage.zip` to a temporary file, checks its size and SHA-256 against the manifest, unpacks the AppImage to a `.part` file next to the installed one, checks that too and renames it over the installed AppImage. A failed check deletes the download.
6. Restarts the `whisker` service if it was running (waiting for a recording to finish), and shows an "Updated to vX.Y.Z" notification if the tray is on.

Every failure is logged and leaves the installed AppImage untouched. There are no retry loops: curl retries a transient error once, and the next try is the next day's run. Source builds and anything not started from the AppImage refuse to update themselves; use `git pull && make install` or the installer.

The Decky plugin isn't updated this way, because its folder belongs to root. Rerun the installer for that.

## Verifying a release by hand

With [minisign](https://jedisct1.github.io/minisign/) and a key from `src/signing_keys.h`:

```sh
minisign -V -P <public key from src/signing_keys.h> -m whisker-manifest.json
sha256sum Whisker-x86_64.AppImage.zip   # compare with the manifest's assets entry
```

The trusted comment minisign prints must read `whisker vX.Y.Z` for the release you downloaded. The manifest's `appimage` entry is the SHA-256 of the AppImage inside the zip.

## For maintainers

### Signing in CI

The `publish` job in `release.yml` runs `scripts/sign-release.sh`, which:

* fails if the `WHISKER_SIGNING_KEY` secret is missing, so a release is never published unsigned
* downloads minisign 0.12 and checks its pinned SHA-256
* writes the manifest with `scripts/make-manifest.py` (fails if the tag doesn't match the version in `CMakeLists.txt` or the zip is compressed)
* signs it with the trusted comment `whisker <tag>`
* fails unless the signature verifies with a key in `src/signing_keys.h`

Release builds pass `-DWHISKER_REQUIRE_SIGNING_KEY=ON`, which adds the `release_signing_key` ctest. It fails while `src/signing_keys.h` still holds the placeholder.

### Creating the first key

1. Install `minisign` and log in with `gh auth login`.
2. Run `scripts/gen-signing-key.sh`. It generates a password-less keypair in `$XDG_RUNTIME_DIR`, stores the secret key as the `WHISKER_SIGNING_KEY` secret of the `release` environment (so only the approved publish job can read it), prints the public key and waits while you copy the secret key into your password manager. Then it shreds the file.
3. Replace the placeholder in `src/signing_keys.h` with the printed public key and merge that before the next release.

A password-protected key works too: generate it with `minisign -G` and also set `WHISKER_SIGNING_KEY_PASSWORD` in the `release` environment.

### Rotating the key

Installed copies only trust the keys they were built with, so rotate in two releases:

1. Run `scripts/gen-signing-key.sh --next`. It makes the new key and saves it only to your password manager. Add its public key to `src/signing_keys.h` next to the old one and release. CI still signs with the old key.
2. Once most users run that release, paste the new secret key from your password manager into `gh secret set WHISKER_SIGNING_KEY --env release -R cgrd/whisker` (end with Ctrl-D), remove the old public key from `src/signing_keys.h`, and release.

Anyone still on a release older than step 1 has to rerun the installer after step 2.

If the secret key leaks, delete the secret, make a new key with `--next`, swap the public keys in `src/signing_keys.h`, set the new secret, release, and publish an advisory: copies that still trust the leaked key accept anything it signs until they update.
