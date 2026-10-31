// SPDX-License-Identifier: MIT
#include "update.h"
#include "sha256.h"
#include "signing_keys.h"

#include <monocypher-ed25519.h>
#include <monocypher.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <unistd.h>

namespace {

bool base64Decode(const std::string& in, std::string& out) {
    static const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (in.empty() || in.size() % 4 != 0) return false;
    out.clear();
    uint32_t acc = 0;
    int bits = 0;
    size_t pad = 0;
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '=') {
            if (i + 2 < in.size()) return false;
            ++pad;
            continue;
        }
        size_t v = alphabet.find(in[i]);
        if (pad || v == std::string::npos) return false;
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((acc >> bits) & 0xff));
        }
    }
    return true;
}

const uint8_t* bytes(const std::string& s, size_t offset = 0) {
    return reinterpret_cast<const uint8_t*>(s.data()) + offset;
}

struct MinisignKey {
    std::string id;
    std::string public_key;
};

bool parsePublicKey(const std::string& text, MinisignKey& key) {
    std::string raw;
    if (!base64Decode(text, raw) || raw.size() != 42 || raw.compare(0, 2, "Ed") != 0) return false;
    key.id = raw.substr(2, 8);
    key.public_key = raw.substr(10, 32);
    return true;
}

// minisign prints key IDs as a little-endian 64-bit number
std::string keyIdHex(const std::string& id) {
    std::string out;
    char buf[3];
    for (size_t i = id.size(); i-- > 0;) {
        snprintf(buf, sizeof(buf), "%02X", static_cast<uint8_t>(id[i]));
        out += buf;
    }
    return out;
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

bool startsWith(const std::string& s, const std::string& prefix) { return s.compare(0, prefix.size(), prefix) == 0; }

struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    uint64_t number = 0;
    std::string text;
    std::vector<Json> items;
    std::vector<std::string> keys;

    const Json* get(const std::string& key) const {
        for (size_t i = 0; i < keys.size(); ++i)
            if (keys[i] == key) return &items[i];
        return nullptr;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& s) : s_(s) {}

    bool parse(Json& out) {
        if (!value(out, 0)) return false;
        skipSpace();
        return i_ == s_.size();
    }

private:
    static constexpr int kMaxDepth = 8;
    const std::string& s_;
    size_t i_ = 0;

    bool more() const { return i_ < s_.size(); }

    void skipSpace() {
        while (more() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_;
    }

    bool literal(const char* word) {
        size_t n = strlen(word);
        if (s_.compare(i_, n, word) != 0) return false;
        i_ += n;
        return true;
    }

    bool value(Json& out, int depth) {
        if (depth > kMaxDepth) return false;
        skipSpace();
        if (!more()) return false;
        char c = s_[i_];
        if (c == '{') return object(out, depth);
        if (c == '[') return array(out, depth);
        if (c == '"') {
            out.type = Json::Type::String;
            return string(out.text);
        }
        if (c >= '0' && c <= '9') return number(out);
        if (c == 't' || c == 'f') {
            out.type = Json::Type::Bool;
            return literal(c == 't' ? "true" : "false");
        }
        return literal("null");
    }

    // Sizes are the only numbers in the manifest, so anything but a plain non-negative integer is rejected
    bool number(Json& out) {
        out.type = Json::Type::Number;
        size_t start = i_;
        uint64_t v = 0;
        while (more() && s_[i_] >= '0' && s_[i_] <= '9') {
            if (v > (1ULL << 53)) return false;
            v = v * 10 + static_cast<uint64_t>(s_[i_++] - '0');
        }
        if (i_ - start > 1 && s_[start] == '0') return false;
        if (more() && (s_[i_] == '.' || s_[i_] == 'e' || s_[i_] == 'E')) return false;
        out.number = v;
        return true;
    }

    bool string(std::string& out) {
        static const char from[] = "\"\\/bfnrt";
        static const char to[] = "\"\\/\b\f\n\r\t";
        ++i_;
        out.clear();
        while (more()) {
            char c = s_[i_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return false;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (!more()) return false;
            const char* p = strchr(from, s_[i_++]);
            if (!p || !*p) return false;
            out += to[p - from];
        }
        return false;
    }

    bool array(Json& out, int depth) {
        out.type = Json::Type::Array;
        ++i_;
        skipSpace();
        if (more() && s_[i_] == ']') {
            ++i_;
            return true;
        }
        while (true) {
            Json item;
            if (!value(item, depth + 1)) return false;
            out.items.push_back(std::move(item));
            skipSpace();
            if (!more()) return false;
            char c = s_[i_++];
            if (c == ']') return true;
            if (c != ',') return false;
        }
    }

    bool object(Json& out, int depth) {
        out.type = Json::Type::Object;
        ++i_;
        skipSpace();
        if (more() && s_[i_] == '}') {
            ++i_;
            return true;
        }
        while (true) {
            skipSpace();
            std::string key;
            if (!more() || s_[i_] != '"' || !string(key) || out.get(key)) return false;
            skipSpace();
            if (!more() || s_[i_++] != ':') return false;
            Json item;
            if (!value(item, depth + 1)) return false;
            out.keys.push_back(std::move(key));
            out.items.push_back(std::move(item));
            skipSpace();
            if (!more()) return false;
            char c = s_[i_++];
            if (c == '}') return true;
            if (c != ',') return false;
        }
    }
};

bool isSha256Hex(const std::string& s) {
    if (s.size() != 64) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

bool digestFrom(const Json* j, FileDigest& out) {
    if (!j || j->type != Json::Type::Object) return false;
    const Json* size = j->get("size");
    const Json* sha = j->get("sha256");
    if (!size || size->type != Json::Type::Number || size->number == 0) return false;
    if (!sha || sha->type != Json::Type::String || !isSha256Hex(sha->text)) return false;
    out.size = size->number;
    out.sha256 = sha->text;
    return true;
}

uint16_t le16(const std::string& d, size_t at) {
    return static_cast<uint16_t>(static_cast<uint8_t>(d[at]) | static_cast<uint8_t>(d[at + 1]) << 8);
}

uint32_t le32(const std::string& d, size_t at) {
    return static_cast<uint32_t>(le16(d, at)) | static_cast<uint32_t>(le16(d, at + 2)) << 16;
}

bool readFile(const std::string& path, std::string& out, uint64_t max_size) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    std::streamoff size = f.tellg();
    if (size < 0 || static_cast<uint64_t>(size) > max_size) return false;
    f.seekg(0);
    out.assign(static_cast<size_t>(size), '\0');
    return static_cast<bool>(f.read(out.data(), size));
}

bool writeFile(const std::string& path, const char* data, size_t len) {
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return false;
    while (len > 0) {
        ssize_t n = write(fd, data, len);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            close(fd);
            return false;
        }
        data += n;
        len -= static_cast<size_t>(n);
    }
    return fsync(fd) == 0 && close(fd) == 0;
}

std::string extractEntry(const std::string& zip_path, const std::string& entry, const std::string& out_path,
                         uint64_t max_size) {
    const std::string malformed = "the update zip is malformed";
    std::string data;
    if (!readFile(zip_path, data, kMaxUpdateBytes + (1 << 20))) return "cannot read the update zip";
    if (data.size() < 22) return malformed;

    size_t eocd = std::string::npos;
    size_t lowest = data.size() > 22 + 0xffff ? data.size() - 22 - 0xffff : 0;
    for (size_t p = data.size() - 22;; --p) {
        if (le32(data, p) == 0x06054b50) {
            eocd = p;
            break;
        }
        if (p == lowest) break;
    }
    if (eocd == std::string::npos) return malformed;
    uint16_t count = le16(data, eocd + 10);
    uint64_t cd_size = le32(data, eocd + 12);
    uint64_t cd_offset = le32(data, eocd + 16);
    if (cd_offset + cd_size > eocd) return malformed;

    uint64_t p = cd_offset;
    for (int n = 0; n < count; ++n) {
        if (p + 46 > eocd || le32(data, p) != 0x02014b50) return malformed;
        uint16_t flags = le16(data, p + 8);
        uint16_t method = le16(data, p + 10);
        uint32_t packed = le32(data, p + 20);
        uint32_t size = le32(data, p + 24);
        uint16_t name_len = le16(data, p + 28);
        uint64_t local = le32(data, p + 42);
        if (p + 46 + name_len > eocd) return malformed;
        std::string name = data.substr(p + 46, name_len);
        p += 46 + name_len + le16(data, p + 30) + le16(data, p + 32);
        if (name != entry) continue;

        if (flags & 1) return "the update zip is encrypted";
        if (method != 0 || packed != size) return "the update zip is compressed; only stored entries are supported";
        if (size > max_size) return "the update is larger than the manifest says";
        if (local + 30 > cd_offset || le32(data, local) != 0x04034b50) return malformed;
        uint64_t start = local + 30 + le16(data, local + 26) + le16(data, local + 28);
        if (start + size > cd_offset) return malformed;
        if (!writeFile(out_path, data.data() + start, size))
            return "cannot write " + out_path + ": " + strerror(errno);
        return "";
    }
    return "the update zip has no " + entry;
}

} // namespace

bool parseVersion(const std::string& text, Version& out, bool allow_v) {
    size_t i = allow_v && !text.empty() && text[0] == 'v' ? 1 : 0;
    for (size_t part = 0; part < out.size(); ++part) {
        size_t start = i;
        uint32_t v = 0;
        while (i < text.size() && i - start < 6 && text[i] >= '0' && text[i] <= '9')
            v = v * 10 + static_cast<uint32_t>(text[i++] - '0');
        size_t len = i - start;
        if (len == 0 || (len > 1 && text[start] == '0')) return false;
        out[part] = v;
        if (part + 1 < out.size()) {
            if (i >= text.size() || text[i] != '.') return false;
            ++i;
        }
    }
    return i == text.size();
}

std::string tagFromReleaseUrl(const std::string& url) {
    const std::string prefix = "https://github.com/cgrd/whisker/releases/tag/";
    if (!startsWith(url, prefix)) return "";
    std::string tag = url.substr(prefix.size());
    Version v;
    if (tag.empty() || tag[0] != 'v' || !parseVersion(tag, v, true)) return "";
    return tag;
}

std::vector<std::string> releaseSigningKeys() {
    return {std::begin(kReleaseSigningKeys), std::end(kReleaseSigningKeys)};
}

bool hasReleaseSigningKey() {
    std::vector<std::string> keys = releaseSigningKeys();
    MinisignKey key;
    for (const auto& text : keys)
        if (text == kSigningKeyPlaceholder || !parsePublicKey(text, key)) return false;
    return !keys.empty();
}

std::string verifyMinisign(const std::string& message, const std::string& signature,
                           const std::vector<std::string>& public_keys, std::string& trusted_comment) {
    std::string hash(64, '\0');
    crypto_blake2b(reinterpret_cast<uint8_t*>(hash.data()), hash.size(), bytes(message), message.size());
    return verifyMinisignDigest(hash, signature, public_keys, trusted_comment);
}

std::string verifyMinisignDigest(const std::string& blake2b_512, const std::string& signature,
                                 const std::vector<std::string>& public_keys, std::string& trusted_comment) {
    const std::string untrusted_prefix = "untrusted comment: ";
    const std::string trusted_prefix = "trusted comment: ";
    trusted_comment.clear();
    if (signature.size() > kMaxSignatureBytes) return "the signature file is too large";
    std::vector<std::string> lines = splitLines(signature);
    std::string sig, global;
    if (lines.size() != 4 || !startsWith(lines[0], untrusted_prefix) || !startsWith(lines[2], trusted_prefix) ||
        !base64Decode(lines[1], sig) || sig.size() != 74 || !base64Decode(lines[3], global) || global.size() != 64)
        return "the signature file is malformed";
    if (sig.compare(0, 2, "ED") != 0) return "the signature is not a prehashed (BLAKE2b) minisign signature";

    std::string key_id = sig.substr(2, 8);
    MinisignKey key;
    bool found = false;
    for (const auto& text : public_keys)
        if (parsePublicKey(text, key) && key.id == key_id) {
            found = true;
            break;
        }
    if (!found) return "the signature is from an unknown key (" + keyIdHex(key_id) + ")";

    if (blake2b_512.size() != 64 ||
        crypto_ed25519_check(bytes(sig, 10), bytes(key.public_key), bytes(blake2b_512), blake2b_512.size()) != 0)
        return "the signature does not match";

    std::string comment = lines[2].substr(trusted_prefix.size());
    std::string signed_comment = sig.substr(10) + comment;
    if (crypto_ed25519_check(bytes(global), bytes(key.public_key), bytes(signed_comment), signed_comment.size()) != 0)
        return "the trusted comment signature does not match";
    trusted_comment = comment;
    return "";
}

std::string parseManifest(const std::string& json, Manifest& out) {
    if (json.size() > kMaxManifestBytes) return "the manifest is too large";
    Json root;
    if (!JsonParser(json).parse(root) || root.type != Json::Type::Object) return "the manifest is not valid JSON";
    const Json* name = root.get("name");
    if (!name || name->type != Json::Type::String || name->text != "whisker") return "the manifest is not for Whisker";
    const Json* version = root.get("version");
    Version v;
    if (!version || version->type != Json::Type::String || !parseVersion(version->text, v))
        return "the manifest has no valid version";
    const Json* assets = root.get("assets");
    if (!assets || assets->type != Json::Type::Object) return "the manifest lists no assets";

    Manifest m;
    m.version = version->text;
    for (size_t i = 0; i < assets->keys.size(); ++i) {
        FileDigest digest;
        if (!digestFrom(&assets->items[i], digest)) return "the manifest entry for " + assets->keys[i] + " is invalid";
        m.assets[assets->keys[i]] = digest;
    }
    if (!m.assets.count(kUpdateZipAsset)) return std::string("the manifest has no entry for ") + kUpdateZipAsset;
    if (!digestFrom(root.get("appimage"), m.appimage)) return "the manifest has no valid appimage entry";
    out = std::move(m);
    return "";
}

std::string checkManifest(const Manifest& manifest, const std::string& tag, const std::string& running,
                          const std::string& trusted_comment) {
    Version tag_version, manifest_version, running_version;
    if (!parseVersion(tag, tag_version, true)) return "invalid release tag '" + tag + "'";
    if (!parseVersion(manifest.version, manifest_version)) return "the manifest has no valid version";
    if (manifest_version != tag_version)
        return "the manifest is for " + manifest.version + " but the release is " + tag;
    if (trusted_comment != "whisker v" + manifest.version)
        return "the signed comment '" + trusted_comment + "' does not name release v" + manifest.version;
    if (!parseVersion(running, running_version)) return "cannot read the running version '" + running + "'";
    if (manifest_version <= running_version)
        return "the manifest version " + manifest.version + " is not newer than " + running;
    auto zip = manifest.assets.find(kUpdateZipAsset);
    if (zip == manifest.assets.end()) return std::string("the manifest has no entry for ") + kUpdateZipAsset;
    if (zip->second.size > kMaxUpdateBytes || manifest.appimage.size > kMaxUpdateBytes)
        return "the update is larger than the " + std::to_string(kMaxUpdateBytes >> 20) + " MiB limit";
    return "";
}

std::string verifyReleaseDir(const std::string& dir, const std::vector<std::string>& public_keys,
                             const std::string& running, std::string& version) {
    namespace fs = std::filesystem;
    const std::string signature_name = std::string(kManifestAsset) + ".minisig";
    std::string manifest_text, signature;
    if (!readFile(dir + "/" + kManifestAsset, manifest_text, kMaxManifestBytes))
        return std::string("cannot read ") + kManifestAsset;
    if (!readFile(dir + "/" + signature_name, signature, kMaxSignatureBytes)) return "cannot read " + signature_name;

    std::string trusted_comment;
    std::string error = verifyMinisign(manifest_text, signature, public_keys, trusted_comment);
    if (!error.empty()) return "the release manifest is not trusted: " + error;
    Manifest manifest;
    error = parseManifest(manifest_text, manifest);
    if (!error.empty()) return error;
    if (trusted_comment != "whisker v" + manifest.version)
        return "the signed comment '" + trusted_comment + "' does not name release v" + manifest.version;
    Version manifest_version, running_version;
    parseVersion(manifest.version, manifest_version);
    if (!parseVersion(running, running_version)) return "cannot read the running version '" + running + "'";
    if (manifest_version < running_version)
        return "the release " + manifest.version + " is older than the installed " + running;

    std::vector<fs::path> files;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        std::string name = entry.path().filename().string();
        if (name != kManifestAsset && name != signature_name) files.push_back(entry.path());
    }
    if (ec) return "cannot list " + dir + ": " + ec.message();
    if (files.empty()) return "there are no release files to verify";
    for (const auto& path : files) {
        std::string name = path.filename().string();
        auto asset = manifest.assets.find(name);
        if (asset == manifest.assets.end()) return name + " is not in the signed manifest";
        if (!fs::is_regular_file(fs::symlink_status(path, ec))) return name + " is not a regular file";
        if (fs::file_size(path, ec) != asset->second.size || ec) return name + " has the wrong size";
        if (sha256File(path.string()) != asset->second.sha256) return name + " does not match its SHA-256";
    }
    version = manifest.version;
    return "";
}

std::string appImageBackupPath(const std::string& appimage) {
    size_t slash = appimage.rfind('/');
    return appimage.substr(0, slash + 1) + "." + appimage.substr(slash + 1) + ".previous";
}

bool restartSurvived(const std::vector<bool>& active_samples) {
    if (active_samples.empty()) return false;
    for (bool active : active_samples)
        if (!active) return false;
    return true;
}

std::string extractStoredZipEntry(const std::string& zip_path, const std::string& entry,
                                  const std::string& out_path, uint64_t max_size) {
    std::string error = extractEntry(zip_path, entry, out_path, max_size);
    if (!error.empty()) unlink(out_path.c_str());
    return error;
}
