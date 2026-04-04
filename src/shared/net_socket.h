#pragma once

/**
 * Simple TCP socket wrapper for client-server communication.
 * Non-blocking I/O using POSIX sockets.
 */

#include "shared/net_protocol.h"
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <netinet/tcp.h>
#include <vector>
#include <cstring>
#include <cstdio>

namespace agentica::net {

// Make a socket non-blocking
inline void setNonBlocking(int fd) {
	int flags = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// Disable Nagle's algorithm — send small packets immediately.
// Without this, TCP buffers small writes for ~40ms causing visible input lag.
inline void setNoDelay(int fd) {
	int flag = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
}

// Send a complete message (header + payload)
inline bool sendMessage(int fd, MsgType type, const WriteBuffer& payload) {
	MsgHeader hdr;
	hdr.type = type;
	hdr.length = (uint32_t)payload.size();

	// Send header
	if (send(fd, &hdr, sizeof(hdr), MSG_NOSIGNAL) != sizeof(hdr)) return false;
	// Send payload
	if (payload.size() > 0) {
		size_t sent = 0;
		while (sent < payload.size()) {
			ssize_t n = send(fd, payload.data().data() + sent, payload.size() - sent, MSG_NOSIGNAL);
			if (n <= 0) return false;
			sent += n;
		}
	}
	return true;
}

// Receive buffer that accumulates partial reads
class RecvBuffer {
public:
	// Drain all available data from socket. Returns false if connection closed.
	// Reads in a loop until EAGAIN to avoid starving on large payloads
	// (each chunk is 16KB, many arrive per frame).
	bool readFrom(int fd) {
		uint8_t tmp[65536]; // 64KB — handles multiple chunks per read
		bool gotData = false;
		for (;;) {
			ssize_t n = recv(fd, tmp, sizeof(tmp), 0);
			if (n > 0) {
				m_buf.insert(m_buf.end(), tmp, tmp + n);
				gotData = true;
				continue; // try to read more
			}
			if (n == 0) return false; // connection closed
			// EAGAIN = no more data right now (non-blocking)
			return gotData || (errno == EAGAIN || errno == EWOULDBLOCK);
		}
	}

	// Try to extract a complete message. Returns true if one is available.
	bool tryExtract(MsgHeader& hdr, std::vector<uint8_t>& payload) {
		if (m_buf.size() < sizeof(MsgHeader)) return false;

		memcpy(&hdr, m_buf.data(), sizeof(MsgHeader));
		size_t total = sizeof(MsgHeader) + hdr.length;
		if (m_buf.size() < total) return false;

		payload.assign(m_buf.begin() + sizeof(MsgHeader),
		               m_buf.begin() + total);
		m_buf.erase(m_buf.begin(), m_buf.begin() + total);
		return true;
	}

private:
	std::vector<uint8_t> m_buf;
};

// ================================================================
// TCP Server Listener
// ================================================================

class TcpServer {
public:
	bool listen(int port) {
		m_fd = socket(AF_INET, SOCK_STREAM, 0);
		if (m_fd < 0) { perror("socket"); return false; }

		int opt = 1;
		setsockopt(m_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = INADDR_ANY;
		addr.sin_port = htons(port);

		if (bind(m_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
			perror("bind"); close(m_fd); m_fd = -1; return false;
		}
		if (::listen(m_fd, 64) < 0) {
			perror("listen"); close(m_fd); m_fd = -1; return false;
		}

		setNonBlocking(m_fd);
		printf("[Net] Listening on port %d\n", port);
		return true;
	}

	struct AcceptResult {
		int fd = -1;
		std::string ip;
		int port = 0;
	};

	// Accept a new connection (non-blocking). Returns fd, IP, and port.
	AcceptResult acceptClient() {
		sockaddr_in addr{};
		socklen_t len = sizeof(addr);
		int fd = accept(m_fd, (sockaddr*)&addr, &len);
		if (fd >= 0) {
			setNonBlocking(fd);
			setNoDelay(fd);
			AcceptResult r;
			r.fd = fd;
			r.ip = inet_ntoa(addr.sin_addr);
			r.port = ntohs(addr.sin_port);
			printf("[Net] Client connected from %s:%d\n", r.ip.c_str(), r.port);
			return r;
		}
		return {};
	}

	void shutdown() {
		if (m_fd >= 0) { close(m_fd); m_fd = -1; }
	}

	int fd() const { return m_fd; }

private:
	int m_fd = -1;
};

// ================================================================
// TCP Client Connection
// ================================================================

class TcpClient {
public:
	// Connect with a short timeout (default 1 second).
	// Uses non-blocking connect + select() to avoid hanging for 30+ seconds
	// when no server is running.
	bool connect(const char* host, int port, float timeoutSec = 1.0f) {
		m_fd = socket(AF_INET, SOCK_STREAM, 0);
		if (m_fd < 0) { perror("socket"); return false; }

		setNonBlocking(m_fd);

		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);
		inet_pton(AF_INET, host, &addr.sin_addr);

		int ret = ::connect(m_fd, (sockaddr*)&addr, sizeof(addr));
		if (ret < 0 && errno != EINPROGRESS) {
			printf("[Net] connect() failed: %s\n", strerror(errno));
			close(m_fd); m_fd = -1; return false;
		}

		if (ret == 0) {
			setNoDelay(m_fd);
			printf("[Net] Connected to %s:%d\n", host, port);
			return true;
		}

		// Wait for connection with timeout using select()
		fd_set wfds;
		FD_ZERO(&wfds);
		FD_SET(m_fd, &wfds);
		struct timeval tv;
		tv.tv_sec = (int)timeoutSec;
		tv.tv_usec = (int)((timeoutSec - (int)timeoutSec) * 1000000);

		ret = select(m_fd + 1, nullptr, &wfds, nullptr, &tv);
		if (ret <= 0) {
			// Timeout or error
			close(m_fd); m_fd = -1; return false;
		}

		// Check if connection succeeded
		int err = 0;
		socklen_t len = sizeof(err);
		getsockopt(m_fd, SOL_SOCKET, SO_ERROR, &err, &len);
		if (err != 0) {
			close(m_fd); m_fd = -1; return false;
		}

		setNoDelay(m_fd);
		printf("[Net] Connected to %s:%d\n", host, port);
		return true;
	}

	void disconnect() {
		if (m_fd >= 0) { close(m_fd); m_fd = -1; }
	}

	int fd() const { return m_fd; }
	bool connected() const { return m_fd >= 0; }

private:
	int m_fd = -1;
};

} // namespace agentica::net
