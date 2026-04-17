#pragma once

// Non-blocking POSIX TCP socket wrappers.

#include "net/net_protocol.h"
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

namespace civcraft::net {

inline void setNonBlocking(int fd) {
	int flags = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// Disable Nagle; otherwise small writes buffer ~40ms → visible input lag.
inline void setNoDelay(int fd) {
	int flag = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
}

// INVARIANT: once hdr is on the wire, exactly hdr.length bytes MUST follow
// or the peer desyncs permanently. Retry EAGAIN (buffer-full during chunk floods).
inline bool sendMessage(int fd, MsgType type, const WriteBuffer& payload) {
	MsgHeader hdr;
	hdr.type = type;
	hdr.length = (uint32_t)payload.size();

	auto sendAll = [fd, type](const void* buf, size_t n) -> bool {
		const uint8_t* p = (const uint8_t*)buf;
		size_t sent = 0;
		int spins = 0;
		while (sent < n) {
			ssize_t w = send(fd, p + sent, n - sent, MSG_NOSIGNAL);
			if (w > 0) { sent += (size_t)w; continue; }
			if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
				// Surface runaway spins so livelock doesn't silently stall the tick loop.
				if (++spins == 10000)
					fprintf(stderr, "[net] sendMessage fd=%d type=%u EAGAIN spin=%d (%zu/%zu bytes)\n",
						fd, (unsigned)type, spins, sent, n);
				continue;
			}
			fprintf(stderr, "[net] sendMessage fd=%d type=%u send() failed: errno=%d (%s) after %zu/%zu bytes\n",
				fd, (unsigned)type, errno, strerror(errno), sent, n);
			return false;
		}
		return true;
	};

	if (!sendAll(&hdr, sizeof(hdr))) return false;
	if (payload.size() > 0 && !sendAll(payload.data().data(), payload.size())) return false;
	return true;
}

class RecvBuffer {
public:
	// Loop until EAGAIN — avoids starving on chunk floods. false = conn closed.
	bool readFrom(int fd) {
		uint8_t tmp[65536];
		bool gotData = false;
		for (;;) {
			ssize_t n = recv(fd, tmp, sizeof(tmp), 0);
			if (n > 0) {
				m_buf.insert(m_buf.end(), tmp, tmp + n);
				gotData = true;
				continue;
			}
			if (n == 0) return false;
			return gotData || (errno == EAGAIN || errno == EWOULDBLOCK);
		}
	}

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

class TcpClient {
public:
	~TcpClient() { disconnect(); }

	// Non-blocking connect + select — prevents 30s hang on dead server.
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

		fd_set wfds;
		FD_ZERO(&wfds);
		FD_SET(m_fd, &wfds);
		struct timeval tv;
		tv.tv_sec = (int)timeoutSec;
		tv.tv_usec = (int)((timeoutSec - (int)timeoutSec) * 1000000);

		ret = select(m_fd + 1, nullptr, &wfds, nullptr, &tv);
		if (ret <= 0) {
			close(m_fd); m_fd = -1; return false;
		}

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

// LAN server discovery: broadcast send + non-blocking recv.
class UdpSocket {
public:
	struct Packet { std::string senderIp; std::string data; };

	// bindPort=0 → ephemeral.
	bool open(int bindPort = 0, bool enableBroadcast = false) {
		m_fd = socket(AF_INET, SOCK_DGRAM, 0);
		if (m_fd < 0) return false;

		int opt = 1;
		setsockopt(m_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
		setsockopt(m_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
		if (enableBroadcast)
			setsockopt(m_fd, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));

		if (bindPort > 0) {
			sockaddr_in addr{};
			addr.sin_family      = AF_INET;
			addr.sin_addr.s_addr = INADDR_ANY;
			addr.sin_port        = htons(bindPort);
			if (bind(m_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
				::close(m_fd); m_fd = -1; return false;
			}
		}
		setNonBlocking(m_fd);
		return true;
	}

	void close() { if (m_fd >= 0) { ::close(m_fd); m_fd = -1; } }
	bool isOpen() const { return m_fd >= 0; }

	bool broadcast(const char* data, int len, int port) {
		sockaddr_in addr{};
		addr.sin_family      = AF_INET;
		addr.sin_port        = htons(port);
		addr.sin_addr.s_addr = INADDR_BROADCAST;
		return sendto(m_fd, data, len, 0, (sockaddr*)&addr, sizeof(addr)) > 0;
	}

	bool tryRecv(Packet& out) {
		char buf[256];
		sockaddr_in addr{};
		socklen_t addrLen = sizeof(addr);
		ssize_t n = recvfrom(m_fd, buf, sizeof(buf) - 1, 0, (sockaddr*)&addr, &addrLen);
		if (n <= 0) return false;
		buf[n] = '\0';
		out.senderIp = inet_ntoa(addr.sin_addr);
		out.data     = buf;
		return true;
	}

private:
	int m_fd = -1;
};

} // namespace civcraft::net
