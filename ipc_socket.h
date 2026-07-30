#pragma once

#include <chrono>
#include <string>

// Raw Unix-domain-socket connection shared by both WM protocol backends.
//
// Connect on AF_UNIX either succeeds immediately or fails immediately with
// a clear errno (ENOENT: socket file missing, ECONNREFUSED: nothing
// listening) — unlike TCP there's no network round-trip to time out, so
// connect itself needs no separate timeout. The real hang risk (Section
// 2A) is a WM that accepted the connection but never responds; that's
// covered by SO_RCVTIMEO on every read.
class UnixSocket {
public:
    // Throws IpcConnectionError (backend.h) if the socket file doesn't
    // exist or the connection is refused.
    UnixSocket(const std::string& path, std::chrono::milliseconds read_timeout);
    ~UnixSocket();
    UnixSocket(const UnixSocket&) = delete;
    UnixSocket& operator=(const UnixSocket&) = delete;

    void write_all(const std::string& data);

    // Reads exactly n bytes. Throws IpcTimeoutError if the read stalls
    // past the configured timeout, IpcMalformedResponseError if the peer
    // closes early (fewer than n bytes available, ever).
    std::string read_exact(size_t n);

    // Reads until the peer closes the connection — Hyprland's protocol has
    // no length prefix, so a response is delimited by EOF. Throws
    // IpcTimeoutError if no EOF arrives within the configured timeout.
    std::string read_until_eof();

private:
    int fd_;
};
