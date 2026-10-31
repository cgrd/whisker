# Changelog

## [1.3.1](https://github.com/cgrd/whisker/compare/v1.3.0...v1.3.1) (2026-10-31)


### Bug Fixes

* **decky:** default the backend to CPU on the Deck ([#43](https://github.com/cgrd/whisker/issues/43)) ([3053c5c](https://github.com/cgrd/whisker/commit/3053c5c52a9f3dae8c6e1b8dca3417be8daa0d4f))

## [1.3.0](https://github.com/cgrd/whisker/compare/v1.2.0...v1.3.0) (2026-10-31)


### Features

* Stadia trigger buttons and game chat profiles ([#37](https://github.com/cgrd/whisker/issues/37)) ([1e9c0dd](https://github.com/cgrd/whisker/commit/1e9c0dd83f63701ea15bdf4fe3b4eaf6f6f5ba3d))
* **update:** signed automatic updates ([#39](https://github.com/cgrd/whisker/issues/39)) ([8416c86](https://github.com/cgrd/whisker/commit/8416c8656f34f5b67da490f54ccb276045c2ead4))

## [1.2.0](https://github.com/cgrd/whisker/compare/v1.1.1...v1.2.0) (2026-10-26)


### Features

* **models:** label models by precision and size from one shared table ([#29](https://github.com/cgrd/whisker/issues/29)) ([b4ca4ca](https://github.com/cgrd/whisker/commit/b4ca4ca9134302de254215afc986397a8b83efea))
* **models:** let the user pick the speech model, never download one by default ([#30](https://github.com/cgrd/whisker/issues/30)) ([d358938](https://github.com/cgrd/whisker/commit/d358938a96fe6e99b5a0cb3889e1d9809c80d316))
* **settings:** add a Triggers page with shortcut and button capture ([#28](https://github.com/cgrd/whisker/issues/28)) ([bfe797f](https://github.com/cgrd/whisker/commit/bfe797f08073e04495105c979fbb9f29fb50b908))


### Bug Fixes

* **install:** set explicit file modes instead of inheriting the caller's umask ([#24](https://github.com/cgrd/whisker/issues/24)) ([28b9382](https://github.com/cgrd/whisker/commit/28b93825557fb4c68a63191e8b69610f102a67d5))
* **models:** pin model downloads to a commit and verify size and SHA-256 ([#31](https://github.com/cgrd/whisker/issues/31)) ([fe4a1ab](https://github.com/cgrd/whisker/commit/fe4a1ab72a11146a429c97c5d3e20b099cdbd03b))
* **settings:** highlight the selected page in the sidebar ([#32](https://github.com/cgrd/whisker/issues/32)) ([1768c93](https://github.com/cgrd/whisker/commit/1768c936d42fa01140ce8100daa583f7bb90ed6a))

## [1.1.1](https://github.com/cgrd/whisker/compare/v1.1.0...v1.1.1) (2026-10-25)


### Bug Fixes

* **install:** report a failed Decky plugin step instead of exiting silently ([#19](https://github.com/cgrd/whisker/issues/19)) ([fd7c791](https://github.com/cgrd/whisker/commit/fd7c79104c7bf3a128b8ca2939ebe878447777c3))
* **settings:** scale rendering to the framebuffer on HiDPI Wayland ([#21](https://github.com/cgrd/whisker/issues/21)) ([7c01e84](https://github.com/cgrd/whisker/commit/7c01e8441aabbc65219617efa1a8a94bc25c28c6))

## [1.1.0](https://github.com/cgrd/whisker/compare/v1.0.0...v1.1.0) (2026-10-25)


### Features

* **input:** push-to-talk from any connected controller ([#12](https://github.com/cgrd/whisker/issues/12)) ([e066b36](https://github.com/cgrd/whisker/commit/e066b36241fa327e4aac924837bf0f15fde3f120))


### Bug Fixes

* **build:** build a portable CPU baseline instead of the runner's CPU ([#14](https://github.com/cgrd/whisker/issues/14)) ([3b042ff](https://github.com/cgrd/whisker/commit/3b042ff95d57ec2063acac396e53711b14cf8bc5))
* **haptic:** rumble only physical controllers and stop every pulse ([#10](https://github.com/cgrd/whisker/issues/10)) ([faff25f](https://github.com/cgrd/whisker/commit/faff25f733c3e1bfb9a7f7453e64b365ce2b78b2))

## 1.0.0 (2026-10-25)


### Features

* initial public release ([5ed0af4](https://github.com/cgrd/whisker/commit/5ed0af45fde8a4bba46e94b1cb1b809022abc58c))
