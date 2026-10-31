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
2. Downloads `whisker-manifest.json` and `whisker-manifest.json.minisig` from that tag over HTTPS, with size and time limits.
3. Checks the minisign signature and its trusted comment against the public keys built into Whisker, and refuses a manifest whose version differs from the tag or isn't newer than the running version, so an old release can't be replayed to downgrade anyone.
4. Asks the daemon over its command socket whether it's recording. If it is, the timer run gives up until tomorrow.
5. Downloads `Whisker-x86_64.AppImage.zip` to a temporary file, checks its size and SHA-256 against the manifest, unpacks the AppImage to a `.part` file next to the installed one, checks that too, keeps the installed AppImage as a hidden `.Whisker-x86_64.AppImage.previous` next to it and renames the new one over it. A failed check deletes the download.
6. Restarts the `whisker` service if it was running (waiting for a recording to finish) and watches it for 12 seconds. If it wasn't active the whole time (it crashed or exited), the update renames the previous AppImage back, restarts it, logs the failure and exits with an error; the next day's run tries again. Otherwise it deletes the previous AppImage and shows an "Updated to vX.Y.Z" notification if the tray is on.

Every failure is logged and leaves the installed AppImage untouched. There are no retry loops: curl retries a transient error once, and the next try is the next day's run. Nothing can hang: every curl call gives up after 30 seconds without a connection or 60 seconds below 1 KB/s, the release check and manifest after 60 seconds and the zip after 30 minutes, and `whisker-update.service` has `TimeoutStartSec=80min`. Curl ignores `~/.curlrc`.

The update lock (`whisker-update.lock`) and the command socket live in `$XDG_RUNTIME_DIR`. Whisker uses that directory only if it belongs to the user and has mode 0700, and refuses to update otherwise, so another user can't hold the lock or impersonate the daemon. Source builds and anything not started from the AppImage refuse to update themselves; use `git pull && make install` or the installer.

The Decky panel's Updates section runs the same checks. Opening the panel runs `--check-update` on the installed AppImage (the one the `whisker` service starts, else `~/Applications/Whisker-x86_64.AppImage`) and shows the version and whether a newer signed release exists. Update runs `--update`; the button is disabled while Whisker is recording, and the result shows below it. The plugin runs only those two fixed commands, kills them if they run longer than 10 minutes (check) or 85 minutes (update), and never polls for updates.

The Decky plugin itself isn't updated this way, because its folder belongs to root. Rerun the installer for that; it has your installed Whisker check the plugin against the signed manifest first (see below).

## Installer

`install.sh` downloads the release assets together with `whisker-manifest.json` and its signature, and checks them before it replaces anything:

* Over an existing install it runs `~/Applications/Whisker-x86_64.AppImage --verify-release <dir>`. The installed Whisker checks the signature with the keys built into it, refuses a release older than itself (the same version is fine, for a reinstall), and checks the size and SHA-256 of the AppImage zip and the Decky plugin zip. Any failure stops the installer with the old install untouched. That makes rerunning the installer as safe as `--update`, for the Decky plugin too.
* A first install has no Whisker to ask, and neither has one over a Whisker too old to know `--verify-release`. With [minisign](https://jedisct1.github.io/minisign/) installed, it checks the signature with the public keys in `install.sh` (a test keeps them equal to `src/signing_keys.h`) and every file against the manifest.
* Without minisign it can only compare the files with `SHA256SUMS`, which comes from the same place. That install trusts what it downloaded from GitHub, like any `curl | bash`, and the installer prints a warning saying so. Every update after that is checked.
* `WHISKER_RELEASE_URL=file://...` builds without a manifest (a local `./build-container.sh`) install with a warning; a remote release without one is refused over an existing install.

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

Anyone still on a release older than step 1 has to reinstall after step 2: their Whisker doesn't trust the new key, so the installer refuses to update over it. `install.sh --uninstall` (settings are kept) and then the install one-liner works, with the first-install trust described under [Installer](#installer).

If the secret key leaks, delete the secret, make a new key with `--next`, swap the public keys in `src/signing_keys.h`, set the new secret, release, and publish an advisory: copies that still trust the leaked key accept anything it signs until they update.
