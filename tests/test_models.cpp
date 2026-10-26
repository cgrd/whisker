// SPDX-License-Identifier: MIT
#include "check.h"
#include "models.h"
#include "sha256.h"

#include <arpa/inet.h>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;

static void testTable() {
    const auto& models = knownModels();
    CHECK_EQ(models.size(), size_t(4));
    CHECK_EQ(modelLabel(models[0]), std::string("Parakeet 0.6B, 8-bit (Q8_0), 731 MB"));
    CHECK_EQ(modelLabel(models[3]), std::string("Parakeet 0.6B, 4-bit (Q4_K_M), 477 MB"));
    CHECK_EQ(modelUrl(models[0]),
             std::string("https://huggingface.co/handy-computer/parakeet-unified-en-0.6b-gguf/resolve/"
                         "843b1b175ace04f5a89d379c79e2518c0d065b25/parakeet-unified-en-0.6b-Q8_0.gguf"));
    for (size_t i = 0; i < models.size(); ++i) {
        CHECK_EQ(models[i].sha256.size(), size_t(64));
        CHECK(!models[i].note.empty());
        if (i == 0) continue;
        CHECK(models[i].size < models[i - 1].size);
        CHECK(models[i].bits < models[i - 1].bits);
    }
    CHECK_EQ(modelOriginalUrl(), std::string("https://huggingface.co/nvidia/parakeet-unified-en-0.6b"));
    CHECK(modelUserAgent().rfind("Whisker/", 0) == 0);
    CHECK(modelUserAgent().find("(+https://github.com/cgrd/whisker)") != std::string::npos);
}

static void testSha256() {
    Sha256 empty;
    CHECK_EQ(empty.hexDigest(), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    Sha256 abc;
    abc.update("abc", 3);
    CHECK_EQ(abc.hexDigest(), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    // Two blocks, fed in uneven pieces
    std::string text = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    Sha256 split;
    split.update(text.data(), 5);
    split.update(text.data() + 5, text.size() - 5);
    CHECK_EQ(split.hexDigest(), std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
}

static void writeFixture(const fs::path& path) { std::ofstream(path) << "abc"; }

static void testInstallVerified(const fs::path& dir) {
    const std::string abc_sha = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    const std::string bad_sha = "0000000000000000000000000000000000000000000000000000000000000000";
    fs::path tmp = dir / ".m.gguf.part";
    fs::path dest = dir / "m.gguf";

    writeFixture(tmp);
    CHECK_EQ(sha256File(tmp.string()), abc_sha);
    std::string error = installVerified(tmp, dest, 3, bad_sha);
    CHECK(error.find("SHA-256") != std::string::npos);
    CHECK(!fs::exists(tmp));
    CHECK(!fs::exists(dest));

    writeFixture(tmp);
    error = installVerified(tmp, dest, 4, abc_sha);
    CHECK(error.find("wrong size") != std::string::npos);
    CHECK(!fs::exists(tmp));
    CHECK(!fs::exists(dest));

    writeFixture(tmp);
    CHECK_EQ(installVerified(tmp, dest, 3, abc_sha), std::string());
    CHECK(!fs::exists(tmp));
    CHECK(fs::exists(dest));

    CHECK(!installVerified(tmp, dest, 3, abc_sha).empty());
}

// Answers every request with 503 and counts them.
struct FailingServer {
    int fd = -1;
    int port = 0;
    std::atomic<int> requests{0};
    std::atomic<bool> stop{false};
    std::thread thread;

    FailingServer() {
        fd = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        CHECK(bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
        listen(fd, 8);
        socklen_t len = sizeof(addr);
        getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
        port = ntohs(addr.sin_port);
        thread = std::thread([this] {
            while (!stop) {
                pollfd p{fd, POLLIN, 0};
                if (poll(&p, 1, 50) <= 0) continue;
                int client = accept(fd, nullptr, nullptr);
                if (client < 0) continue;
                char buf[4096];
                (void)!read(client, buf, sizeof(buf));
                ++requests;
                const char* reply = "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                (void)!write(client, reply, strlen(reply));
                close(client);
            }
        });
    }
    ~FailingServer() {
        stop = true;
        thread.join();
        close(fd);
    }
};

static void testFailedDownloadIsRetriedAtMostOnce(const fs::path& dir) {
    FailingServer server;
    fs::path tmp = dir / ".fail.part";
    std::string error = downloadFile("http://127.0.0.1:" + std::to_string(server.port) + "/m.gguf", tmp.string());
    std::printf("failing download: %d requests, error: %s\n", server.requests.load(), error.c_str());
    CHECK(!error.empty());
    CHECK(server.requests >= 1);
    CHECK(server.requests <= 2);
}

int main() {
    fs::path dir = fs::temp_directory_path() / ("whisker-test-models-" + std::to_string(getpid()));
    fs::create_directories(dir);
    testTable();
    testSha256();
    testInstallVerified(dir);
    testFailedDownloadIsRetriedAtMostOnce(dir);
    fs::remove_all(dir);
    return checkReport();
}
