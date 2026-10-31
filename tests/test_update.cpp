// SPDX-License-Identifier: MIT
#include "check.h"
#include "sha256.h"
#include "update.h"

#include <monocypher-ed25519.h>
#include <monocypher.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

std::string base64(const std::string& in) {
    static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < in.size(); i += 3) {
        uint32_t n = static_cast<uint8_t>(in[i]) << 16;
        if (i + 1 < in.size()) n |= static_cast<uint8_t>(in[i + 1]) << 8;
        if (i + 2 < in.size()) n |= static_cast<uint8_t>(in[i + 2]);
        out += alphabet[(n >> 18) & 63];
        out += alphabet[(n >> 12) & 63];
        out += i + 1 < in.size() ? alphabet[(n >> 6) & 63] : '=';
        out += i + 2 < in.size() ? alphabet[n & 63] : '=';
    }
    return out;
}

std::string fromHex(const std::string& hex) {
    std::string out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) out += static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16));
    return out;
}

const uint8_t* bytes(const std::string& s) { return reinterpret_cast<const uint8_t*>(s.data()); }

// A throwaway minisign keypair derived from a fixed seed. TEST ONLY, never a release key.
struct TestKey {
    uint8_t secret[64];
    uint8_t pub[32];
    std::string id;

    TestKey(uint8_t seed_byte, const std::string& key_id) : id(key_id) {
        uint8_t seed[32];
        memset(seed, seed_byte, sizeof(seed));
        crypto_ed25519_key_pair(secret, pub, seed);
    }

    std::string publicKey() const { return base64("Ed" + id + std::string(reinterpret_cast<const char*>(pub), 32)); }

    std::string sign(const std::string& message, const std::string& comment, const char* algorithm = "ED") const {
        uint8_t hash[64];
        crypto_blake2b(hash, sizeof(hash), bytes(message), message.size());
        uint8_t sig[64];
        if (strcmp(algorithm, "ED") == 0)
            crypto_ed25519_sign(sig, secret, hash, sizeof(hash));
        else
            crypto_ed25519_sign(sig, secret, bytes(message), message.size());
        std::string sig_bytes(reinterpret_cast<const char*>(sig), 64);
        std::string global_message = sig_bytes + comment;
        uint8_t global[64];
        crypto_ed25519_sign(global, secret, bytes(global_message), global_message.size());
        return "untrusted comment: signature from minisign secret key\n" + base64(algorithm + id + sig_bytes) +
               "\ntrusted comment: " + comment + "\n" +
               base64(std::string(reinterpret_cast<const char*>(global), 64)) + "\n";
    }
};

const std::string kSha = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

std::string manifestJson(const std::string& version, uint64_t zip_size = 15500958) {
    return "{\n"
           "  \"name\": \"whisker\",\n"
           "  \"version\": \"" + version + "\",\n"
           "  \"assets\": {\n"
           "    \"Whisker-x86_64.AppImage.zip\": {\"size\": " + std::to_string(zip_size) + ", \"sha256\": \"" + kSha + "\"},\n"
           "    \"install.sh\": {\"size\": 9315, \"sha256\": \"" + kSha + "\"}\n"
           "  },\n"
           "  \"appimage\": {\"size\": 16005624, \"sha256\": \"" + kSha + "\"}\n"
           "}\n";
}

std::string readAll(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

} // namespace

static void testParseVersion() {
    Version v;
    CHECK(parseVersion("1.2.3", v));
    CHECK(v == Version{1, 2, 3});
    CHECK(parseVersion("v10.0.12", v, true));
    CHECK(v == Version{10, 0, 12});
    CHECK(parseVersion("0.0.0", v));
    for (const char* bad : {"", "1.2", "1.2.3.4", "01.2.3", "1.2.3-rc1", "1..3", "v1.2.3", "1.2.x", " 1.2.3",
                            "1234567.0.0", "1.2.3\n"})
        CHECK(!parseVersion(bad, v));
    CHECK(!parseVersion("V1.2.3", v, true));
    CHECK(!parseVersion("vv1.2.3", v, true));
    CHECK(Version{1, 10, 0} > Version{1, 9, 99});
    CHECK(Version{2, 0, 0} > Version{1, 99, 99});
}

static void testTagFromReleaseUrl() {
    CHECK_EQ(tagFromReleaseUrl("https://github.com/cgrd/whisker/releases/tag/v1.3.0"), "v1.3.0");
    CHECK_EQ(tagFromReleaseUrl("https://github.com/cgrd/whisker/releases/tag/1.3.0"), "");
    CHECK_EQ(tagFromReleaseUrl("https://github.com/cgrd/whisker/releases/tag/v1.3.0/../x"), "");
    CHECK_EQ(tagFromReleaseUrl("https://github.com/evil/whisker/releases/tag/v1.3.0"), "");
    CHECK_EQ(tagFromReleaseUrl("http://github.com/cgrd/whisker/releases/tag/v1.3.0"), "");
    CHECK_EQ(tagFromReleaseUrl("https://github.com/cgrd/whisker/releases"), "");
    CHECK_EQ(tagFromReleaseUrl(""), "");
}

static void testVerifySignature() {
    TestKey key(7, "testkey1");
    TestKey other(9, "testkey2");
    std::string manifest = manifestJson("1.3.0");
    std::string sig = key.sign(manifest, "whisker v1.3.0");
    std::string comment;

    CHECK_EQ(verifyMinisign(manifest, sig, {key.publicKey()}, comment), "");
    CHECK_EQ(comment, "whisker v1.3.0");

    // Rotation: any key in the list may sign
    CHECK_EQ(verifyMinisign(manifest, other.sign(manifest, "c"), {key.publicKey(), other.publicKey()}, comment), "");

    CHECK_EQ(verifyMinisign(manifest, sig, {other.publicKey()}, comment),
             "the signature is from an unknown key (3179656B74736574)");
    CHECK(comment.empty());

    // A different key that claims the trusted key's ID
    TestKey impostor(9, "testkey1");
    CHECK_EQ(verifyMinisign(manifest, impostor.sign(manifest, "whisker v1.3.0"), {key.publicKey()}, comment),
             "the signature does not match");

    std::string tampered = manifest;
    tampered[tampered.find("1.3.0")] = '9';
    CHECK_EQ(verifyMinisign(tampered, sig, {key.publicKey()}, comment), "the signature does not match");

    std::string forged_comment = sig;
    forged_comment.replace(forged_comment.find("whisker v1.3.0"), 14, "whisker v9.3.0");
    CHECK_EQ(verifyMinisign(manifest, forged_comment, {key.publicKey()}, comment),
             "the trusted comment signature does not match");
    CHECK(comment.empty());

    CHECK_EQ(verifyMinisign(manifest, key.sign(manifest, "whisker v1.3.0", "Ed"), {key.publicKey()}, comment),
             "the signature is not a prehashed (BLAKE2b) minisign signature");

    std::string no_trailing_newline = sig.substr(0, sig.size() - 1);
    CHECK_EQ(verifyMinisign(manifest, no_trailing_newline, {key.publicKey()}, comment), "");
    for (const std::string& bad : {std::string(), sig.substr(0, sig.find("\ntrusted")), sig + "extra\n",
                                   std::string("untrusted comment: x\n!!!!\ntrusted comment: y\nAAAA\n"),
                                   "garbage" + sig.substr(sig.find('\n'))})
        CHECK_EQ(verifyMinisign(manifest, bad, {key.publicKey()}, comment), "the signature file is malformed");
    CHECK_EQ(verifyMinisign(manifest, sig, {}, comment).rfind("the signature is from an unknown key", 0), size_t(0));
    CHECK(verifyMinisign(manifest, sig, {"not a key", kSigningKeyPlaceholder}, comment).find("unknown key") !=
          std::string::npos);
    CHECK_EQ(verifyMinisign(manifest, std::string(kMaxSignatureBytes + 1, 'a'), {key.publicKey()}, comment),
             "the signature file is too large");
}

// Frank Denis's signature of minisign-0.12-linux.tar.gz, made by minisign itself, checks that the
// format handling matches the real tool and not only this test's signer.
static void testRealMinisignSignature() {
    const std::string pub = "RWQf6LRCGA9i53mlYecO4IzT51TGPpvWucNSCh1CBM0QTaLn73Y7GFO3";
    const std::string sig =
        "untrusted comment: signature from minisign secret key\n"
        "RUQf6LRCGA9i51+vFPSVJsPgR4eqpoQSwrDeyS6ZjSiKVYmcyNzZvI5GrBSeAip2d2WC66JGJD8Yd8kcHWwrYBjIuJPyLHahrwQ=\n"
        "trusted comment: timestamp:1737030580\tfile:minisign-0.12-linux.tar.gz\thashed\n"
        "fvukCLJv91XcKTSSFFksyWOFz/3qwhRs3bXTW7TtSX2AQVFPx7IRjwEvXkMNsPoF3UXG9rSb+uSg0nOb55v6CQ==\n";
    const std::string blake2b = fromHex(
        "1b2f5c4b31cfd76ff7fab663c5dd796a2869b4e5ec29ae9d1049d7f8c281ed2b"
        "89992a0c1d180b7022ff5278c41038fd8d5613d041d2c6c4d1e8963a32e1c5e8");
    std::string comment;
    CHECK_EQ(verifyMinisignDigest(blake2b, sig, {pub}, comment), "");
    CHECK_EQ(comment, "timestamp:1737030580\tfile:minisign-0.12-linux.tar.gz\thashed");
    std::string wrong = blake2b;
    wrong[0] ^= 1;
    CHECK_EQ(verifyMinisignDigest(wrong, sig, {pub}, comment), "the signature does not match");
}

static void testParseManifest() {
    Manifest m;
    CHECK_EQ(parseManifest(manifestJson("1.3.0"), m), "");
    CHECK_EQ(m.version, "1.3.0");
    CHECK_EQ(m.assets.size(), size_t(2));
    CHECK_EQ(m.assets.at("Whisker-x86_64.AppImage.zip").size, uint64_t(15500958));
    CHECK_EQ(m.assets.at("install.sh").sha256, kSha);
    CHECK_EQ(m.appimage.size, uint64_t(16005624));

    // Fields a later release may add are ignored
    std::string extended = manifestJson("1.3.0");
    extended.insert(1, "\"notes\": [1, true, null, {\"a\": \"b\\n\"}], \"schema\": 2,");
    CHECK_EQ(parseManifest(extended, m), "");

    std::string zip_entry = "\"Whisker-x86_64.AppImage.zip\"";
    std::string good = manifestJson("1.3.0");
    auto replaced = [&](const std::string& from, const std::string& to) {
        std::string s = good;
        s.replace(s.find(from), from.size(), to);
        return s;
    };
    CHECK_EQ(parseManifest(replaced(zip_entry, "\"other.zip\""), m),
             "the manifest has no entry for Whisker-x86_64.AppImage.zip");
    CHECK_EQ(parseManifest(replaced("\"version\": \"1.3.0\"", "\"version\": \"1.3\""), m),
             "the manifest has no valid version");
    CHECK_EQ(parseManifest(replaced("\"name\": \"whisker\"", "\"name\": \"other\""), m),
             "the manifest is not for Whisker");
    CHECK_EQ(parseManifest(replaced("15500958", "0"), m),
             "the manifest entry for Whisker-x86_64.AppImage.zip is invalid");
    CHECK_EQ(parseManifest(replaced("15500958", "1.5"), m), "the manifest is not valid JSON");
    CHECK_EQ(parseManifest(replaced("15500958", "-1"), m), "the manifest is not valid JSON");
    CHECK_EQ(parseManifest(replaced("15500958", "99999999999999999999"), m), "the manifest is not valid JSON");
    CHECK_EQ(parseManifest(replaced("\"size\": 9315", "\"size\": \"9315\""), m),
             "the manifest entry for install.sh is invalid");
    CHECK_EQ(parseManifest(replaced("0123456789abcdef", "0123456789ABCDEF"), m),
             "the manifest entry for Whisker-x86_64.AppImage.zip is invalid");
    CHECK_EQ(parseManifest(replaced("\"appimage\"", "\"app\""), m), "the manifest has no valid appimage entry");
    CHECK_EQ(parseManifest(replaced("\"name\": \"whisker\",", "\"name\": \"whisker\", \"name\": \"whisker\","), m),
             "the manifest is not valid JSON");
    CHECK_EQ(parseManifest(good + "x", m), "the manifest is not valid JSON");
    CHECK_EQ(parseManifest(good.substr(0, good.size() / 2), m), "the manifest is not valid JSON");
    CHECK_EQ(parseManifest("[]", m), "the manifest is not valid JSON");
    CHECK_EQ(parseManifest("", m), "the manifest is not valid JSON");
    CHECK_EQ(parseManifest(std::string(50, '[') + std::string(50, ']'), m), "the manifest is not valid JSON");
    CHECK_EQ(parseManifest(std::string(kMaxManifestBytes + 1, ' '), m), "the manifest is too large");
    CHECK_EQ(m.version, "1.3.0");
}

static void testCheckManifest() {
    Manifest m;
    parseManifest(manifestJson("1.3.0"), m);
    CHECK_EQ(checkManifest(m, "v1.3.0", "1.2.0", "whisker v1.3.0"), "");
    CHECK_EQ(checkManifest(m, "v1.3.0", "1.2.9", "whisker v1.3.0"), "");

    CHECK_EQ(checkManifest(m, "v1.3.0", "1.3.0", "whisker v1.3.0"),
             "the manifest version 1.3.0 is not newer than 1.3.0");
    CHECK_EQ(checkManifest(m, "v1.3.0", "1.10.0", "whisker v1.3.0"),
             "the manifest version 1.3.0 is not newer than 1.10.0");
    // A replayed old manifest under a newer tag
    CHECK_EQ(checkManifest(m, "v1.4.0", "1.2.0", "whisker v1.3.0"), "the manifest is for 1.3.0 but the release is v1.4.0");
    CHECK_EQ(checkManifest(m, "v1.3.0", "1.2.0", "whisker v1.4.0"),
             "the signed comment 'whisker v1.4.0' does not name release v1.3.0");
    CHECK_EQ(checkManifest(m, "v1.3.0", "1.2.0", "timestamp:1 file:whisker-manifest.json"),
             "the signed comment 'timestamp:1 file:whisker-manifest.json' does not name release v1.3.0");
    CHECK_EQ(checkManifest(m, "latest", "1.2.0", "whisker v1.3.0"), "invalid release tag 'latest'");
    CHECK_EQ(checkManifest(m, "v1.3.0", "dev", "whisker v1.3.0"), "cannot read the running version 'dev'");

    parseManifest(manifestJson("1.3.0", kMaxUpdateBytes), m);
    CHECK_EQ(checkManifest(m, "v1.3.0", "1.2.0", "whisker v1.3.0"), "");
    parseManifest(manifestJson("1.3.0", kMaxUpdateBytes + 1), m);
    CHECK_EQ(checkManifest(m, "v1.3.0", "1.2.0", "whisker v1.3.0"), "the update is larger than the 256 MiB limit");
}

// Made by Python's zipfile with ZIP_STORED, as scripts/package-release.sh does: notes.txt ("other\n")
// and Whisker-x86_64.AppImage ("fake appimage\n")
const std::string kStoredZipHex =
    "504b0304140000000000b3a45d5d6e7e60090600000006000000090000006e6f7465732e7478746f746865720a504b03"
    "04140000000000b3a45d5d224dfb320e0000000e00000017000000576869736b65722d7838365f36342e417070496d61"
    "676566616b6520617070696d6167650a504b01021403140000000000b3a45d5d6e7e6009060000000600000009000000"
    "0000000000000000a481000000006e6f7465732e747874504b01021403140000000000b3a45d5d224dfb320e0000000e"
    "000000170000000000000000000000a4812d000000576869736b65722d7838365f36342e417070496d616765504b0506"
    "00000000020002007c000000700000000000";

static void testExtractZip() {
    fs::path dir = fs::temp_directory_path() / ("whisker-test-update-" + std::to_string(getpid()));
    fs::create_directories(dir);
    auto write = [&](const char* name, const std::string& data) {
        fs::path path = dir / name;
        std::ofstream(path, std::ios::binary) << data;
        return path.string();
    };
    std::string zip = fromHex(kStoredZipHex);
    std::string stored = write("stored.zip", zip);
    std::string out = (dir / "app.part").string();

    CHECK_EQ(extractStoredZipEntry(stored, kUpdateAppImageEntry, out, 1000), "");
    CHECK_EQ(readAll(out), "fake appimage\n");
    CHECK_EQ(extractStoredZipEntry(stored, "notes.txt", out, 1000), "");
    CHECK_EQ(readAll(out), "other\n");

    CHECK_EQ(extractStoredZipEntry(stored, kUpdateAppImageEntry, out, 13), "the update is larger than the manifest says");
    CHECK(!fs::exists(out));
    std::ofstream(out) << "stale";
    CHECK_EQ(extractStoredZipEntry(stored, "missing", out, 1000), "the update zip has no missing");
    CHECK(!fs::exists(out));

    std::string deflated = zip;
    size_t central = deflated.find(std::string("PK\x01\x02", 4), deflated.find("Whisker-x86_64.AppImage") + 1);
    central = deflated.find(std::string("PK\x01\x02", 4), central + 1);
    deflated[central + 10] = 8;
    CHECK_EQ(extractStoredZipEntry(write("deflated.zip", deflated), kUpdateAppImageEntry, out, 1000),
             "the update zip is compressed; only stored entries are supported");
    CHECK(!fs::exists(out));

    std::string encrypted = zip;
    encrypted[central + 8] = 1;
    CHECK_EQ(extractStoredZipEntry(write("encrypted.zip", encrypted), kUpdateAppImageEntry, out, 1000),
             "the update zip is encrypted");

    // Entry data pointing past the central directory
    std::string overlong = zip;
    overlong[central + 20] = overlong[central + 24] = 0x7f;
    CHECK_EQ(extractStoredZipEntry(write("overlong.zip", overlong), kUpdateAppImageEntry, out, 1000),
             "the update zip is malformed");

    CHECK_EQ(extractStoredZipEntry(write("truncated.zip", zip.substr(0, zip.size() - 30)), kUpdateAppImageEntry, out,
                                   1000),
             "the update zip is malformed");
    CHECK_EQ(extractStoredZipEntry(write("empty.zip", ""), kUpdateAppImageEntry, out, 1000),
             "the update zip is malformed");
    CHECK_EQ(extractStoredZipEntry((dir / "none.zip").string(), kUpdateAppImageEntry, out, 1000),
             "cannot read the update zip");
    CHECK(!fs::exists(out));
    fs::remove_all(dir);
}

static std::string sha256Hex(const std::string& data) {
    Sha256 sha;
    sha.update(data.data(), data.size());
    return sha.hexDigest();
}

static std::string digestJson(const std::string& data) {
    return "{\"size\": " + std::to_string(data.size()) + ", \"sha256\": \"" + sha256Hex(data) + "\"}";
}

// A release directory as install.sh downloads it, signed by key.
static void writeRelease(const fs::path& dir, const TestKey& key, const std::string& version,
                         const std::string& comment = "") {
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string zip = "zip bytes", decky = "decky bytes";
    std::ofstream(dir / kUpdateZipAsset) << zip;
    std::ofstream(dir / "Whisker-decky.zip") << decky;
    std::string manifest = "{\"name\": \"whisker\", \"version\": \"" + version + "\", \"assets\": {" +
                           "\"" + kUpdateZipAsset + "\": " + digestJson(zip) + ", \"Whisker-decky.zip\": " +
                           digestJson(decky) + "}, \"appimage\": " + digestJson("appimage") + "}\n";
    std::ofstream(dir / kManifestAsset) << manifest;
    std::ofstream(dir / (std::string(kManifestAsset) + ".minisig"))
        << key.sign(manifest, comment.empty() ? "whisker v" + version : comment);
}

static void testVerifyReleaseDir() {
    TestKey key(7, "12345678"), other(8, "87654321");
    std::vector<std::string> keys = {key.publicKey()};
    fs::path dir = fs::temp_directory_path() / ("whisker-test-release-" + std::to_string(getpid()));
    std::string version;

    writeRelease(dir, key, "1.4.0");
    CHECK_EQ(verifyReleaseDir(dir.string(), keys, "1.3.1", version), std::string());
    CHECK_EQ(version, std::string("1.4.0"));
    // Reinstalling the same version is fine, going back is not
    CHECK_EQ(verifyReleaseDir(dir.string(), keys, "1.4.0", version), std::string());
    CHECK(verifyReleaseDir(dir.string(), keys, "1.5.0", version).find("older") != std::string::npos);

    writeRelease(dir, other, "1.4.0");
    CHECK(verifyReleaseDir(dir.string(), keys, "1.3.1", version).find("unknown key") != std::string::npos);

    writeRelease(dir, key, "1.4.0", "whisker v1.9.0");
    CHECK(verifyReleaseDir(dir.string(), keys, "1.3.1", version).find("signed comment") != std::string::npos);

    writeRelease(dir, key, "1.4.0");
    std::ofstream(dir / "Whisker-decky.zip") << "tampered!!!";
    CHECK(verifyReleaseDir(dir.string(), keys, "1.3.1", version).find("Whisker-decky.zip") != std::string::npos);

    writeRelease(dir, key, "1.4.0");
    std::ofstream(dir / "extra.sh") << "echo hi";
    CHECK(verifyReleaseDir(dir.string(), keys, "1.3.1", version).find("not in the signed manifest") !=
          std::string::npos);

    writeRelease(dir, key, "1.4.0");
    fs::remove(dir / "Whisker-decky.zip");
    fs::create_symlink(dir / kUpdateZipAsset, dir / "Whisker-decky.zip");
    CHECK(verifyReleaseDir(dir.string(), keys, "1.3.1", version).find("regular file") != std::string::npos);

    writeRelease(dir, key, "1.4.0");
    fs::remove(dir / (std::string(kManifestAsset) + ".minisig"));
    CHECK(!verifyReleaseDir(dir.string(), keys, "1.3.1", version).empty());

    writeRelease(dir, key, "1.4.0");
    fs::remove(dir / kUpdateZipAsset);
    fs::remove(dir / "Whisker-decky.zip");
    CHECK(verifyReleaseDir(dir.string(), keys, "1.3.1", version).find("no release files") != std::string::npos);
    fs::remove_all(dir);
}

static void testRollbackDecision() {
    CHECK_EQ(appImageBackupPath("/apps/Whisker-x86_64.AppImage"),
             std::string("/apps/.Whisker-x86_64.AppImage.previous"));
    CHECK(restartSurvived(std::vector<bool>(kRestartWatchSeconds, true)));
    CHECK(!restartSurvived({}));
    // Crashed and came back after RestartSec, or exited and stayed down
    CHECK(!restartSurvived({true, true, false, false, false, true, true}));
    CHECK(!restartSurvived({true, true, true, false}));
    CHECK(!restartSurvived({false, true, true, true}));
    CHECK(kRestartWatchSeconds >= 10 && kRestartWatchSeconds <= 15);
}

static void testSigningKeys() {
    auto keys = releaseSigningKeys();
    CHECK(!keys.empty());
    CHECK(hasReleaseSigningKey() || keys == std::vector<std::string>{kSigningKeyPlaceholder});
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "--require-release-key") == 0) {
        if (hasReleaseSigningKey()) return 0;
        fprintf(stderr, "src/signing_keys.h has no valid release signing key (placeholder still in place?). "
                        "See docs/updates.md\n");
        return 1;
    }
    testParseVersion();
    testTagFromReleaseUrl();
    testVerifySignature();
    testRealMinisignSignature();
    testParseManifest();
    testCheckManifest();
    testExtractZip();
    testVerifyReleaseDir();
    testRollbackDecision();
    testSigningKeys();
    return checkReport();
}
