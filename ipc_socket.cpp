#include "ipc_socket.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "backend.h"

UnixSocket::UnixSocket(const std::string& path, std::chrono::milliseconds read_timeout) {
    fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd_ < 0) {
        throw IpcConnectionError("could not create socket: " + std::string(std::strerror(errno)));
    }

    struct timeval tv;
    tv.tv_sec = read_timeout.count() / 1000;
    tv.tv_usec = (read_timeout.count() % 1000) * 1000;
    ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) {
        ::close(fd_);
        throw IpcConnectionError("socket path too long: " + path);
    }
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        int err = errno;
        ::close(fd_);
        throw IpcConnectionError("could not connect to " + path + ": " + std::strerror(err));
    }
}

UnixSocket::~UnixSocket() {
    if (fd_ >= 0) ::close(fd_);
}

void UnixSocket::write_all(const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = ::write(fd_, data.data() + sent, data.size() - sent);
        if (n < 0) {
            throw IpcConnectionError("write failed: " + std::string(std::strerror(errno)));
        }
        sent += static_cast<size_t>(n);
    }
}

std::string UnixSocket::read_exact(size_t n) {
    std::string out;
    out.resize(n);
    size_t got = 0;
    while (got < n) {
        ssize_t r = ::read(fd_, &out[got], n - got);
        if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                throw IpcTimeoutError("read timed out waiting for WM response");
            }
            throw IpcConnectionError("read failed: " + std::string(std::strerror(errno)));
        }
        if (r == 0) {
            throw IpcMalformedResponseError("WM closed connection mid-response");
        }
        got += static_cast<size_t>(r);
    }
    return out;
}

std::string UnixSocket::read_until_eof() {
    std::string out;
    char buf[4096];
    while (true) {
        ssize_t r = ::read(fd_, buf, sizeof(buf));
        if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                throw IpcTimeoutError("read timed out waiting for WM response");
            }
            throw IpcConnectionError("read failed: " + std::string(std::strerror(errno)));
        }
        if (r == 0) break;  // EOF — end of response, not an error for this protocol
        out.append(buf, static_cast<size_t>(r));
    }
    return out;
}
