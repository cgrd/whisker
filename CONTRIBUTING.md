# Contributing to Whisker

Thanks for helping. This covers how to get a change merged.

## Workflow

1. Open an issue first for anything bigger than a small fix, so we can agree on the approach before you write code.
2. Fork https://github.com/cgrd/whisker on GitHub and clone your fork:
   ```sh
   git clone https://github.com/<you>/whisker && cd whisker
   git remote add upstream https://github.com/cgrd/whisker
   ```
3. Branch off the latest main:
   ```sh
   git fetch upstream && git switch -c fix-deckpad-timeout upstream/main
   ```
4. Run `make tools hooks` once. It installs the pinned linters into `.venv` and the git hooks (conventions and secret scan on commit, DCO sign-off check).
5. Make your change, with tests where it makes sense. Run `make check`, which runs everything CI runs on a PR except the container build.
6. Commit with a sign-off (see below): `git commit -s`.
7. Push to your fork and open a pull request against `main`. Copy the template, keep every line, tick every box and fill in `What`, `Tested`, `Tested on`, `Fixes` (#N or none) and `AI-assisted` (yes or no). The Conventions check fails until it's complete.
8. CI runs. A maintainer has to approve CI runs for outside contributors, so the first run may wait a bit.
9. Address review comments by pushing more commits to the same branch. The PR is squash-merged, so don't worry about a tidy history.

Limits and housekeeping:

* Until one of your PRs is merged you can have one open PR; after that, five. PRs over the limit are closed automatically with a comment; reopen yours when another is merged or closed.
* PRs that conflict with `main` get the `needs rebase` label, and `stale` after 7 quiet days. They aren't closed. Rebase on current `main` and force-push (`--force-with-lease`).
* Say whether AI tools helped write the change (`AI-assisted: yes/no`). Either is fine; you're responsible for every line.

PR titles must follow [Conventional Commits](https://www.conventionalcommits.org/) because the title becomes the squash commit and drives the changelog and version number:

* `fix(deckpad): ignore reports shorter than 64 bytes`
* `feat(overlay): support GNOME via a fallback window`
* `docs: explain the Chiaki button conflict`

Use `feat` for new behavior, `fix` for bug fixes, and `docs`, `test`, `refactor`, `build`, `ci` or `chore` for the rest. Add `!` (`feat!:`) for breaking changes.

## Build and test

You need CMake, a C++17 compiler, and the libraries listed in the [README](README.md#build-from-source). Python 3 and pytest for the Decky plugin tests.

```sh
make                     # build into build/
make test                # ctest (C++ unit and CLI smoke tests) + pytest (decky/tests, scripts/tests)
make lint                # shellcheck, ruff, actionlint, zizmor, REUSE
make conventions         # the PR conventions check against origin/main, including uncommitted files
make check               # all of the above
make check-full          # plus the release container build and the install script test (podman)
make hil-test            # virtual controllers (uhid, uinput, via sudo) against a private Whisker instance
ctest --test-dir build   # C++ tests only
./build/whisker --check-config -c config.yaml
```

`scripts/check_conventions.py` enforces the mechanical rules on this page: trailing whitespace, final newlines, SPDX headers, no secrets, hostnames or personal paths, pinned Actions and FetchContent refs, registered tests, doc links and style, no stray notes files, and the PR title and template. In CI it runs from the base branch against your PR's commits.

The Decky UI is TypeScript in `decky/src/`. Rebuild the committed bundle with `make decky` (needs Node and pnpm) and commit `decky/dist/index.js` with your change; `make check` and CI fail if the bundle doesn't match the source.

Tests live in `tests/` (C++, plain asserts in `tests/check.h`, registered in `tests/CMakeLists.txt`) and `decky/tests/` (pytest). They cover pure logic only: config parsing, key combos, dictation text processing, submit key handling, controller report decoding and overlay backend selection. To add a controller, see [docs/controllers.md](docs/controllers.md). Hardware paths are tested by hand.

To build the release AppImage the way CI does: `./build-container.sh` (needs podman). It writes everything to `dist/`. To test the installer against that build instead of the latest GitHub release:

```sh
WHISKER_RELEASE_URL=file://$PWD/dist bash scripts/install.sh
```

If you have a Steam Deck, `DECK_HOST=<deck-host> make deck-test` installs your build on it over SSH and checks that the service starts, survives restarts and logs no errors. `make desktop-smoke` does the same on your desktop. Both replace the installed Whisker.

## Sign your commits (DCO)

Every commit needs a `Signed-off-by` line matching the commit author:

```
Signed-off-by: Your Name <you@example.com>
```

`git commit -s` adds it. By signing off you certify the [Developer Certificate of Origin](https://developercertificate.org/): that you wrote the change or otherwise have the right to submit it under the project's MIT license. There's no CLA.

A CI check fails if any commit in the PR lacks it. To fix existing commits:

```sh
git rebase --signoff upstream/main
git push --force-with-lease
```

## Code style

Match the code around you.

* C++17, 4-space indent, `camelCase` functions, `snake_case_` members, as in `src/`
* Python follows the existing `decky/main.py` style
* Small, clear functions over clever ones. Don't swallow errors.
* Comments are rare. Write one only to explain why something unusual is done, never what the code does.
* No trailing whitespace, and end every file with a newline
* New source files start with the same one-line SPDX MIT header as the existing files in `src/` and `scripts/`
* Third-party libraries are linked statically in release builds. Don't add new runtime dependencies on shared libraries without discussing it first.
* Features must work on desktop Linux in general, not just the Steam Deck. Detect capabilities at runtime (sockets, devices, protocols) instead of checking for a distro or device name.
* Shell scripts must pass `shellcheck -S warning`.

## Keep PRs small

One fix or feature per PR. Small PRs get reviewed faster and are easier to revert. Split refactors from behavior changes, and don't mix in formatting changes to code you didn't otherwise touch.

## What reviewers check

* It does what it says, with tests for any pure logic it adds or changes
* Input from devices, the command socket and config files is validated
* No new dynamic library dependencies; static linking policy kept
* Works on desktop and Deck, or clearly says which one it targets and why
* Docs, the sample `config.yaml` and the Decky plugin are updated if options change
* CI is green, including DCO, REUSE and the Conventions check

Expect a first review within about a week. If it's been longer, a friendly ping on the PR is fine.

## Reporting bugs and security issues

Use the bug report form and include the `journalctl --user -u whisker` output. Report security problems privately as described in [SECURITY.md](SECURITY.md).
