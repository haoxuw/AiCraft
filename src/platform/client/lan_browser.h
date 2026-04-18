#pragma once

// LAN server discovery.
//
// civcraft-server broadcasts "CIVCRAFT <port> <humans>" on
// CIVCRAFT_DISCOVER_PORT (UDP 7778) every 2s (client_manager.h :: announceOnLAN).
// This header-only class binds that port, drains packets non-blocking each tick,
// and maintains a short list of live servers for the Multiplayer menu.

#include "logic/constants.h"
#include "net/net_socket.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace civcraft {

class LanBrowser {
public:
	struct Server {
		std::string ip;
		int port   = 0;
		int humans = 0;
		float firstSeen = 0.0f;
		float lastSeen  = 0.0f;
	};

	// Call every frame in Menu state. wallTime is monotonic seconds.
	void tick(float wallTime) {
		if (!m_udp.isOpen()) {
			// SO_REUSEPORT in UdpSocket::open lets several clients on the same
			// machine co-bind; a bind failure still leaves us usable (servers()
			// just stays empty and listening() reports false).
			if (!m_udp.open(CIVCRAFT_DISCOVER_PORT, false)) {
				if (!m_warned) {
					std::printf("[LAN] Failed to bind UDP %d for discovery\n",
						CIVCRAFT_DISCOVER_PORT);
					m_warned = true;
				}
				return;
			}
			std::printf("[LAN] Listening on UDP %d\n", CIVCRAFT_DISCOVER_PORT);
		}

		net::UdpSocket::Packet pkt;
		while (m_udp.tryRecv(pkt)) {
			int port = 0, humans = 0;
			if (std::sscanf(pkt.data.c_str(), "CIVCRAFT %d %d", &port, &humans) != 2)
				continue;
			if (port <= 0 || port > 65535) continue;

			auto it = std::find_if(m_servers.begin(), m_servers.end(),
				[&](const Server& s) { return s.ip == pkt.senderIp && s.port == port; });
			if (it == m_servers.end()) {
				m_servers.push_back({pkt.senderIp, port, humans, wallTime, wallTime});
				std::printf("[LAN] Discovered %s:%d (%d player%s)\n",
					pkt.senderIp.c_str(), port, humans, humans == 1 ? "" : "s");
			} else {
				it->humans   = humans;
				it->lastSeen = wallTime;
			}
		}

		// Age out entries we haven't heard from in 6s (3× broadcast interval).
		constexpr float kTTL = 6.0f;
		m_servers.erase(std::remove_if(m_servers.begin(), m_servers.end(),
			[&](const Server& s) { return wallTime - s.lastSeen > kTTL; }),
			m_servers.end());
	}

	const std::vector<Server>& servers() const { return m_servers; }
	bool listening() const { return m_udp.isOpen(); }

private:
	net::UdpSocket     m_udp;
	std::vector<Server> m_servers;
	bool m_warned = false;
};

} // namespace civcraft
