# Maintaining Whisker

The playbook for @cgrd. Most of the lifecycle is automated; this lists what runs on its own and what still needs a human.

## What runs automatically

* **Every PR and push to main** (`ci.yml`): container build of the AppImage with ctest inside it, pytest for the Decky plugin, a headless smoke test of the AppImage on plain Arch, `install.sh` tests on Arch, Debian and Ubuntu, shellcheck, actionlint, zizmor, REUSE lint and (PRs only) the DCO check. PR builds upload the release zips as a workflow artifact, so you can download and try a PR build without building it (after reading the diff, see below).
* **Every PR**: the Conventions check (`conventions.yml`: rules, PR title and template, run from the base branch against the PR's objects, plus the risky-change summary), dependency review, CodeQL.
* **Hourly** (`pr-housekeeping.yml`): closes PRs over the open PR limit (1 until an author has a merged PR, 5 after), labels PRs that conflict with main `needs rebase`, applies path labels.
* **Merge to main** (`release.yml`): release-please updates an open "chore(main): release x.y.z" PR with the next version and `CHANGELOG.md`, based on the Conventional Commit titles merged since the last release.
* **Merging the release PR**: release-please tags `vX.Y.Z` and creates the GitHub release. The same workflow builds the assets, waits for approval on the `release` environment, generates build provenance, attaches the zips, `install.sh`, the desktop launchers, `SHA256SUMS` and `Whisker-lgpl-sources.tar`, and puts the install one-liner at the top of the notes.
* **Weekly**: a build against live Arch packages and transcribe.cpp main opens or updates an issue labeled `scheduled-build` if it fails (`scheduled.yml`). `deps-bump.yml` opens PRs for new transcribe.cpp commits and Dear ImGui releases. Dependabot bumps pinned GitHub Actions. Scorecard and CodeQL run on a schedule.
* **Daily** (`stale.yml`): issues and PRs labeled `needs-info` get `no-response` after 30 quiet days and close 14 days later; PRs with `needs rebase` get `stale` after 7 quiet days but stay open. Anything labeled `security` or `pinned` is never touched.

## What you still do by hand

* Read, review and approve PRs; approve CI runs for outside contributors
* Run `make deck-test` before merging PRs that touch `src/controllers*`, `src/buttons*`, `src/haptic*`, `src/overlay*`, `decky/`, the service unit or the AppImage, and before releasing
* Merge the release PR when you're ready to ship, then approve the `release` environment deployment
* Triage new issues
* Merge or close dependency PRs once CI is green

## Triage

Labels:

* `bug`: something broken (added by the bug form)
* `enhancement`: feature request (added by the feature form)
* `good first issue`: small, well-scoped, with pointers to the code
* `needs-info`: waiting on the reporter; goes stale automatically
* `wontfix`: out of scope; close with a short reason
* `security`, `pinned`: exempt from stale handling
* Path labels (`decky`, `overlay`, `deck-input`, `docs`, `ci`, `tests`) are applied to PRs automatically

Labels are defined in `.github/workflows/labels.yml` and synced when that file changes on main. Run the Labels workflow once by hand after creating the repo.

For a bug, ask for the version, `--check-config` output and `journalctl --user -u whisker` if the form is incomplete, and add `needs-info`.

## Reviewing PRs safely

Treat every fork PR as untrusted until you've read all of it.

* Never run a fork's code locally, on the Deck (including `make deck-test`) or by downloading its CI artifact before reading the full diff. The Decky plugin runs as root on the Deck.
* Review in the GitHub UI. The Conventions job summary ("Review flags") lists changes that need a closer look.
* Red flags:
  * changes to `.github/`, `scripts/install.sh`, `Containerfile`, `build-container.sh`, `appimage/`, FetchContent entries in `CMakeLists.txt` or `decky/dist/index.js`
  * new download URLs or domains, `curl`/`wget`, piping into a shell, `eval`, base64 or hex blobs
  * binary files, minified or generated code, very long lines
  * new dependencies, especially names close to well-known ones (typosquatting); check who publishes a new dependency and how long it has existed
  * large "refactor", "cleanup" or formatting PRs from new accounts, which hide small malicious changes
  * changes to how updates, the model or the install script are downloaded or verified
* Report a malicious PR or account to GitHub with "Report content" on the PR or the user's profile, close the PR, and block the user from the repo if needed.
* Protect the maintainer account: 2FA with a passkey or security key, no SMS. Don't hand out write access casually; outside contributors don't need it.

## Review checklist

* Correct and minimal; does what the title says
* Input handling is safe: hidraw report lengths and IDs, evdev events, uinput output (no stuck keys, `releaseAll` on exit), the command socket (only `start`/`stop`/`capture`, bounded buffer), config values, `exec_press`/`exec_release`
* No new dynamic dependencies. Third-party libraries are linked statically; only system-dependent libraries (glibc, Vulkan, PipeWire, libsystemd, X11/Wayland client libraries) stay dynamic.
* Works on desktop Linux and the Deck, with capability detection rather than distro or device checks
* Tests for new pure logic; `make test` passes
* Docs, `config.yaml` and the Decky plugin are updated with new options
* License: new third-party code or assets are listed in `THIRD_PARTY_NOTICES.md` and their license text is in `LICENSES/`

## Merging

* Squash merge only. The PR title becomes the commit message, so fix it to a proper Conventional Commit (`fix(deckpad): ...`) before merging if needed.
* Review settings: with one maintainer the main ruleset requires 0 approvals and no code owner review. When a second maintainer joins, set approvals to 1 and turn code owner review on. Never merge with admin bypass.
* Check out a PR locally (after reading it):
  ```sh
  gh pr checkout 123
  ```
* Stale PRs: ask once what's left. If there's no answer, add `needs-info` and let the bot close it.
* Saying no: thank them, explain the reason in one or two sentences (scope, maintenance cost, platform support), suggest an alternative if there is one, and close. For example: "Thanks for this. Whisker sticks to local transcription, so I'm not going to add a cloud backend. A fork is the best home for it."

## Releases

Versions follow semver; release-please derives them from PR titles (`fix` bumps patch, `feat` bumps minor, `!` bumps major). The first release is 1.0.0 (`initial-version` in `release-please-config.json`), created once the first `feat:` or `fix:` PR lands.

1. Merge PRs as usual. The release PR updates itself.
2. Before shipping: run `DECK_HOST=<deck-host> make deck-test` and `make desktop-smoke` against a build of main.
3. Merge the release PR when ready.
4. Approve the `release` deployment in the Actions tab. The workflow attaches the assets and install notes.
5. Spot-check the release page and run the install one-liner on one machine.

To rebuild assets for an existing tag, run the Release workflow manually with that tag.

PRs opened with the default `GITHUB_TOKEN` count as coming from an outside contributor, so their CI waits for "Approve and run" and the Conventions check never starts. The workflows use the release app token to avoid that (see Release app). The Conventions check skips the template for release-please's own PR.

### Release app

The `release` and `deps-bump` workflows mint a token for a GitHub App with `actions/create-github-app-token`. Pushes made with it trigger workflows, so CI and the required Conventions check run on the release PR and the bump PRs without a close and reopen.

App permissions: Contents, Pull requests and Issues read/write, nothing else. No webhook. Install it only on cgrd/whisker. Then store its ID and a private key:

```sh
gh variable set RELEASE_APP_ID --repo cgrd/whisker --body <app-id>
gh secret set RELEASE_APP_PRIVATE_KEY --repo cgrd/whisker < release-app.private-key.pem
```

Rotate the key by generating a new one in the app settings, updating the secret, then deleting the old key in the app settings.

Without the variable the workflows fall back to `GITHUB_TOKEN` and log a warning. Everything still works, but close and reopen the release PR (or a bump PR) so the required checks run, and approve its CI runs.

## Security reports

1. Reports arrive as private advisories (Security tab). Acknowledge within 7 days.
2. Reproduce and assess severity. Ask the reporter in the advisory thread.
3. Fix in a temporary private fork from the advisory ("Start a temporary private fork"), so the fix isn't public before the release.
4. Merge, release, then publish the advisory with credit and request a CVE from GitHub if it warrants one.

## Dependency updates

* GitHub Actions: Dependabot PRs (`ci: bump ...`). Actions are pinned to full commit SHAs; check the release notes before merging.
* transcribe.cpp and Dear ImGui: `deps-bump.yml` PRs. They change the pins in `CMakeLists.txt`; CI builds and tests them. Read the upstream changes first, and run `make deck-test` for transcribe.cpp bumps since inference speed on the Deck matters.
* Static C libraries (cairo, SDL2, freetype, ...) and the Arch snapshot: update the versions and `ARCH_MIRROR` in `Containerfile` by hand every few months, then update `THIRD_PARTY_NOTICES.md`.
* The scheduled build warns early when upstream changes break the build.

## One-time GitHub setup

Do these once after creating the repo. Commands assume `gh auth login` as @cgrd.

Repository settings (squash only, PR title as commit message, delete merged branches, sign-off required for web commits):

```sh
gh api -X PATCH repos/cgrd/whisker \
  -F allow_squash_merge=true -F allow_merge_commit=false -F allow_rebase_merge=false \
  -f squash_merge_commit_title=PR_TITLE -f squash_merge_commit_message=PR_BODY \
  -F delete_branch_on_merge=true -F web_commit_signoff_required=true \
  -F has_discussions=false
```

Set `has_discussions=true` if you want Discussions for questions.

Security features:

```sh
gh api -X PUT repos/cgrd/whisker/private-vulnerability-reporting
gh api -X PUT repos/cgrd/whisker/vulnerability-alerts
gh api -X PUT repos/cgrd/whisker/automated-security-fixes
```

Actions: read-only default token, allow Actions to open PRs (release-please, deps-bump), and require approval before running workflows from any outside contributor:

```sh
gh api -X PUT repos/cgrd/whisker/actions/permissions/workflow \
  -f default_workflow_permissions=read -F can_approve_pull_request_reviews=true
gh api -X PUT repos/cgrd/whisker/actions/permissions/fork-pr-contributor-approval \
  -f approval_policy=all_external_contributors
```

Release environment, deployable only from main and only after your approval:

```sh
gh api -X PUT repos/cgrd/whisker/environments/release --input - <<EOF
{
  "reviewers": [{"type": "User", "id": $(gh api users/cgrd --jq .id)}],
  "deployment_branch_policy": {"protected_branches": false, "custom_branch_policies": true}
}
EOF
gh api -X POST repos/cgrd/whisker/environments/release/deployment-branch-policies -f name=main -f type=branch
```

Ruleset for main: PR required, 1 approval, code owner review, stale approvals dismissed on push, squash only, required checks, no force pushes or deletion, admins can bypass:

```sh
gh api -X POST repos/cgrd/whisker/rulesets --input - <<'EOF'
{
  "name": "main",
  "target": "branch",
  "enforcement": "active",
  "conditions": {"ref_name": {"include": ["~DEFAULT_BRANCH"], "exclude": []}},
  "bypass_actors": [{"actor_id": 5, "actor_type": "RepositoryRole", "bypass_mode": "always"}],
  "rules": [
    {"type": "deletion"},
    {"type": "non_fast_forward"},
    {"type": "pull_request", "parameters": {
      "required_approving_review_count": 1,
      "dismiss_stale_reviews_on_push": true,
      "require_code_owner_review": true,
      "require_last_push_approval": false,
      "required_review_thread_resolution": true,
      "allowed_merge_methods": ["squash"]
    }},
    {"type": "required_status_checks", "parameters": {
      "strict_required_status_checks_policy": false,
      "required_status_checks": [
        {"context": "build / build"},
        {"context": "smoke"},
        {"context": "install-script (archlinux:base)"},
        {"context": "install-script (debian:trixie)"},
        {"context": "install-script (ubuntu:24.04)"},
        {"context": "unit-python"},
        {"context": "decky-bundle"},
        {"context": "lint"},
        {"context": "dco"},
        {"context": "conventions"}
      ]
    }}
  ]
}
EOF
```

Check names must match what the first CI run reports; adjust them in Settings > Rules if GitHub shows them differently.

Click in the web UI:

* Settings > Code security: make sure CodeQL "default setup" is off, since `codeql.yml` is the advanced setup
* Settings > Secrets and variables > Actions: add the `RELEASE_APP_ID` variable and `RELEASE_APP_PRIVATE_KEY` secret (see Release app)
* Your account: 2FA with a passkey or security key

## Follow-ups

* **Decky plugin store.** Submit to [SteamDeckHomebrew/decky-plugin-database](https://github.com/SteamDeckHomebrew/decky-plugin-database) so Deck users can install from Decky. The submission is a PR adding the plugin repo as a git submodule under `plugins/`, using their "Plugin addition" template. What it needs:
  * a plugin folder in the layout of [decky-plugin-template](https://github.com/SteamDeckHomebrew/decky-plugin-template), with the frontend built from source by their CI (pnpm + rollup). `decky/` already has that layout (`src/index.tsx`, `package.json`, `pnpm-lock.yaml`, `rollup.config.js`), but the database expects the plugin at the root of its own repository, so it would need a separate repo or a subtree.
  * `plugin.json` with `name`, `author`, `flags`, `api_version` and `publish.tags`, `publish.description`, `publish.image` (a hosted screenshot URL; currently empty)
  * `package.json` with a bumped `version` and `license`, and a `LICENSE` file in the plugin root
  * testing on the Stable and Beta SteamOS channels, and ideally testing two other submitted plugins
  * the checklist asks you to confirm that you abide by all library licenses (`THIRD_PARTY_NOTICES.md` covers this) and that generative AI did not write the majority of the submitted code. Answer that one honestly; it may rule out submitting the current plugin code.
* An "Install as background service" button in the settings window for people who start the AppImage by double-click. The AppImage already installs the service on first launch, so this is low priority.
* Overlay support on GNOME, which lacks wlr-layer-shell.
