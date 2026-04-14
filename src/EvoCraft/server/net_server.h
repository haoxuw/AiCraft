// EvoCraft TCP server — single-threaded poll() loop, POSIX sockets.
//
// Handshake: on accept() we send S_HELLO, embedding the reserved player cell
// ID so the client knows which cell it controls.
//
// Inbound parsing: clients can send C_PLAYER_INPUT (8-byte body) any number
// of times per tick. We keep the most recent (vx, vz) per client and merge
// across clients by summing — single-player only matters for now.

#pragma once

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include "net_protocol.h"

namespace evocraft {

class NetServer {
public:
	using PlayerInputFn = std::function<void(float vx, float vz)>;
	using BuyPartFn     = std::function<void(uint8_t kind, float angle, float distance)>;
	using ResetPartsFn  = std::function<void()>;

	explicit NetServer(std::string name) : serverName_(std::move(name)) {}
	~NetServer() { shutdown(); }

	NetServer(const NetServer&) = delete;
	NetServer& operator=(const NetServer&) = delete;

	bool listen(uint16_t port) {
		listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
		if (listenFd_ < 0) { std::perror("[evocraft-server] socket"); return false; }

		int yes = 1;
		::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

		sockaddr_in addr{};
		addr.sin_family      = AF_INET;
		addr.sin_addr.s_addr = INADDR_ANY;
		addr.sin_port        = htons(port);

		if (::bind(listenFd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
			std::perror("[evocraft-server] bind");
			return false;
		}
		if (::listen(listenFd_, 8) < 0) {
			std::perror("[evocraft-server] listen");
			return false;
		}
		setNonBlocking(listenFd_);
		std::fprintf(stderr, "[evocraft-server] listening on 0.0.0.0:%u\n",
			(unsigned)port);
		return true;
	}

	// Caller registers a sink for the latest C_PLAYER_INPUT. Called as soon
	// as a complete frame is parsed — typically multiple times per server
	// tick, since clients send at their own framerate.
	void onPlayerInput(PlayerInputFn fn) { playerInputFn_ = std::move(fn); }
	void onBuyPart(BuyPartFn fn)         { buyPartFn_     = std::move(fn); }
	void onResetParts(ResetPartsFn fn)   { resetPartsFn_  = std::move(fn); }

	// Poll for accept/read events. timeoutMs caps the wait so the caller's
	// tick cadence is preserved even when no I/O happens.
	void poll(int timeoutMs) {
		std::vector<pollfd> fds;
		fds.reserve(1 + clients_.size());
		fds.push_back({listenFd_, POLLIN, 0});
		for (int fd : clients_) fds.push_back({fd, POLLIN, 0});

		int n = ::poll(fds.data(), (nfds_t)fds.size(), timeoutMs);
		if (n <= 0) return;

		if (fds[0].revents & POLLIN) acceptOne();

		for (size_t i = 1; i < fds.size(); ++i) {
			if (fds[i].revents & (POLLIN | POLLHUP | POLLERR)) {
				drainOrClose(fds[i].fd);
			}
		}
	}

	// Send a pre-framed message to every connected client. Drops any client
	// whose write fails (peer closed, SIGPIPE-like).
	void broadcast(const std::vector<uint8_t>& msg) {
		for (auto it = clients_.begin(); it != clients_.end();) {
			if (sendAll(*it, msg.data(), msg.size())) {
				++it;
			} else {
				std::fprintf(stderr,
					"[evocraft-server] client fd=%d dropped on write\n", *it);
				::close(*it);
				inbox_.erase(*it);
				it = clients_.erase(it);
			}
		}
	}

	size_t clientCount() const { return clients_.size(); }

	void shutdown() {
		for (int fd : clients_) ::close(fd);
		clients_.clear();
		inbox_.clear();
		if (listenFd_ >= 0) { ::close(listenFd_); listenFd_ = -1; }
	}

private:
	static void setNonBlocking(int fd) {
		int flags = ::fcntl(fd, F_GETFL, 0);
		if (flags >= 0) ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	}

	void acceptOne() {
		sockaddr_in caddr{};
		socklen_t clen = sizeof(caddr);
		int cfd = ::accept(listenFd_, (sockaddr*)&caddr, &clen);
		if (cfd < 0) {
			if (errno != EAGAIN && errno != EWOULDBLOCK) std::perror("accept");
			return;
		}
		// Disable Nagle so 1Hz heartbeats and player input land promptly.
		int yes = 1;
		::setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));

		auto hello = net::build_s_hello(serverName_, net::PLAYER_CELL_ID);
		if (!sendAll(cfd, hello.data(), hello.size())) {
			::close(cfd);
			return;
		}
		clients_.push_back(cfd);
		inbox_[cfd] = {};
		std::fprintf(stderr,
			"[evocraft-server] accepted client fd=%d from %s:%u (total=%zu)\n",
			cfd, ::inet_ntoa(caddr.sin_addr),
			(unsigned)ntohs(caddr.sin_port), clients_.size());
	}

	void drainOrClose(int fd) {
		uint8_t buf[1024];
		ssize_t r = ::recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
		if (r == 0 || (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
			std::fprintf(stderr,
				"[evocraft-server] client fd=%d closed (r=%zd errno=%d)\n",
				fd, r, (r < 0 ? errno : 0));
			::close(fd);
			inbox_.erase(fd);
			for (auto it = clients_.begin(); it != clients_.end(); ++it) {
				if (*it == fd) { clients_.erase(it); break; }
			}
			return;
		}
		if (r > 0) {
			auto& q = inbox_[fd];
			q.insert(q.end(), buf, buf + r);
			parseFrames(fd);
		}
	}

	// Pop framed messages off the per-client inbox and dispatch them.
	void parseFrames(int fd) {
		auto& q = inbox_[fd];
		while (q.size() >= 4) {
			uint32_t payloadLen = net::unpack_u32(q.data());
			if (q.size() < 4u + payloadLen) return;
			if (payloadLen < 2) {
				// Malformed: drop everything to resync.
				q.clear();
				return;
			}
			uint16_t mtype = net::unpack_u16(q.data() + 4);
			const uint8_t* body  = q.data() + 4 + 2;
			size_t          blen = payloadLen - 2;
			handleMessage(mtype, body, blen);
			q.erase(q.begin(), q.begin() + 4 + payloadLen);
		}
	}

	void handleMessage(uint16_t mtype, const uint8_t* body, size_t blen) {
		switch (mtype) {
			case net::C_PLAYER_INPUT: {
				net::PlayerInput in{};
				if (net::parse_c_player_input(body, blen, in) &&
				    playerInputFn_) {
					playerInputFn_(in.vx, in.vz);
				}
				break;
			}
			case net::C_BUY_PART: {
				net::BuyPart bp{};
				if (net::parse_c_buy_part(body, blen, bp) && buyPartFn_) {
					buyPartFn_(bp.kind, bp.angle, bp.distance);
				}
				break;
			}
			case net::C_RESET_PARTS: {
				if (resetPartsFn_) resetPartsFn_();
				break;
			}
			default:
				// Unknown / not yet handled — silently drop. A noisy log
				// would flood at the client's send rate.
				break;
		}
	}

	bool sendAll(int fd, const uint8_t* data, size_t n) {
		size_t sent = 0;
		while (sent < n) {
			ssize_t w = ::send(fd, data + sent, n - sent, MSG_NOSIGNAL);
			if (w < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
				return false;
			}
			if (w == 0) return false;
			sent += (size_t)w;
		}
		return true;
	}

	std::string                                 serverName_;
	int                                         listenFd_ = -1;
	std::vector<int>                            clients_;
	std::unordered_map<int, std::vector<uint8_t>> inbox_;
	PlayerInputFn                               playerInputFn_;
	BuyPartFn                                   buyPartFn_;
	ResetPartsFn                                resetPartsFn_;
};

} // namespace evocraft
