// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

constexpr uint64_t kMaxManifestBytes = 64 * 1024;
constexpr uint64_t kMaxSignatureBytes = 4 * 1024;
constexpr uint64_t kMaxUpdateBytes = 256ULL * 1024 * 1024;

constexpr const char* kUpdateZipAsset = "Whisker-x86_64.AppImage.zip";
constexpr const char* kUpdateAppImageEntry = "Whisker-x86_64.AppImage";
constexpr const char* kManifestAsset = "whisker-manifest.json";
constexpr const char* kSigningKeyPlaceholder = "PLACEHOLDER-RELEASE-SIGNING-KEY";

using Version = std::array<uint32_t, 3>;

// "1.2.3" or, with allow_v, "v1.2.3". Nothing else: no pre-release suffixes or leading zeros.
bool parseVersion(const std::string& text, Version& out, bool allow_v = false);
// The tag from GitHub's /releases/latest redirect, or "" if the URL isn't a release tag of this repo.
std::string tagFromReleaseUrl(const std::string& url);

// The public keys compiled into this build (src/signing_keys.h), placeholder included.
std::vector<std::string> releaseSigningKeys();
// True if every compiled-in key is a valid minisign public key and none is the placeholder.
bool hasReleaseSigningKey();

// Verifies a minisign signature file over message, prehashed (BLAKE2b-512) Ed25519 only, against
// a list of minisign public keys ("RWQ..."). Checks the global signature over the trusted comment
// too and returns that comment in trusted_comment. Returns "" or an error.
std::string verifyMinisign(const std::string& message, const std::string& signature,
                           const std::vector<std::string>& public_keys, std::string& trusted_comment);
// The same for a message already hashed with BLAKE2b-512, which is what minisign signs.
std::string verifyMinisignDigest(const std::string& blake2b_512, const std::string& signature,
                                 const std::vector<std::string>& public_keys, std::string& trusted_comment);

struct FileDigest {
    uint64_t size = 0;
    std::string sha256;
};

struct Manifest {
    std::string version;
    std::map<std::string, FileDigest> assets;
    FileDigest appimage;
};

// Parses whisker-manifest.json. Returns "" or an error.
std::string parseManifest(const std::string& json, Manifest& out);

// Rejects a verified manifest that doesn't belong to tag, isn't newer than running, carries a
// trusted comment other than "whisker <tag>" or describes a download over the size cap.
std::string checkManifest(const Manifest& manifest, const std::string& tag, const std::string& running,
                          const std::string& trusted_comment);

// Copies an uncompressed (stored) entry out of a zip file to out_path, at most max_size bytes.
// Deletes out_path on any failure. Returns "" or an error.
std::string extractStoredZipEntry(const std::string& zip_path, const std::string& entry,
                                  const std::string& out_path, uint64_t max_size);
