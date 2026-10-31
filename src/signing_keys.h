// SPDX-License-Identifier: MIT
#pragma once

// minisign public keys (the second line of the .pub file) trusted to sign release manifests.
// Release builds fail while the placeholder is here. When rotating, ship the new key next to the
// old one for at least one release before retiring the old one (docs/updates.md).
static const char* const kReleaseSigningKeys[] = {
    "RWQ/sZmHKddY47iThBcwcn10e6QWu5WIpnAqDMDe3k/C4NBhud42ple8",
};
