#include "llm/llm_client.h"

#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <algorithm>

namespace civcraft::llm {

namespace {

// ---- tiny helpers ----------------------------------------------------------

// Open a blocking TCP socket with a connect timeout. Returns -1 on failure.
// Uses select() so a wedged sidecar doesn't block the worker thread for 30s.
int tcpConnectTimed(const std::string& host, int port, float timeoutSec) {
	int fd = ::socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) return -1;

	// Non-blocking during connect, then flip back to blocking for simpler I/O.
	int flags = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
		// Fall through via gethostbyname for "localhost" etc.
		hostent* he = gethostbyname(host.c_str());
		if (!he || he->h_addrtype != AF_INET) { ::close(fd); return -1; }
		memcpy(&addr.sin_addr, he->h_addr_list[0], sizeof(in_addr));
	}

	int rc = ::connect(fd, (sockaddr*)&addr, sizeof(addr));
	if (rc == 0) {
		fcntl(fd, F_SETFL, flags);
		int one = 1;
		setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
		return fd;
	}
	if (errno != EINPROGRESS) { ::close(fd); return -1; }

	fd_set wfds; FD_ZERO(&wfds); FD_SET(fd, &wfds);
	timeval tv;
	tv.tv_sec  = (int)timeoutSec;
	tv.tv_usec = (int)((timeoutSec - (int)timeoutSec) * 1'000'000);
	rc = ::select(fd + 1, nullptr, &wfds, nullptr, &tv);
	if (rc <= 0) { ::close(fd); return -1; }

	int err = 0; socklen_t len = sizeof(err);
	getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
	if (err != 0) { ::close(fd); return -1; }

	fcntl(fd, F_SETFL, flags);
	int one = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	return fd;
}

bool sendAll(int fd, const char* buf, size_t n) {
	size_t sent = 0;
	while (sent < n) {
		ssize_t w = ::send(fd, buf + sent, n - sent, MSG_NOSIGNAL);
		if (w > 0) { sent += (size_t)w; continue; }
		if (w < 0 && (errno == EINTR)) continue;
		return false;
	}
	return true;
}

// Read a single '\n'-terminated line (CRLF tolerated). Returns empty on EOF/err.
// Caps at 64k to avoid unbounded memory from a malicious peer.
std::string recvLine(int fd) {
	std::string out;
	char c;
	while (out.size() < 65536) {
		ssize_t n = ::recv(fd, &c, 1, 0);
		if (n <= 0) return out;
		out.push_back(c);
		if (c == '\n') return out;
	}
	return out;
}

// ---- JSON escaping + minimal parsing --------------------------------------

void jsonEscapeInto(std::string& out, const std::string& s) {
	for (char c : s) {
		switch (c) {
			case '"':  out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n";  break;
			case '\r': out += "\\r";  break;
			case '\t': out += "\\t";  break;
			case '\b': out += "\\b";  break;
			case '\f': out += "\\f";  break;
			default:
				if ((unsigned char)c < 0x20) {
					char buf[8];
					snprintf(buf, sizeof(buf), "\\u%04x", c);
					out += buf;
				} else {
					out.push_back(c);
				}
		}
	}
}

// Find "key":"value" in a JSON blob and unescape the string. Not a full parser;
// good enough for extracting delta.content from llama-server SSE payloads,
// which are single-object-per-line with well-formed escaping.
bool extractJsonString(const std::string& json, const std::string& key, std::string& out) {
	std::string needle = "\"" + key + "\"";
	size_t p = 0;
	while ((p = json.find(needle, p)) != std::string::npos) {
		p += needle.size();
		// Skip whitespace + ':'
		while (p < json.size() && (json[p] == ' ' || json[p] == '\t')) ++p;
		if (p >= json.size() || json[p] != ':') continue;
		++p;
		while (p < json.size() && (json[p] == ' ' || json[p] == '\t')) ++p;
		if (p >= json.size() || json[p] != '"') continue;
		++p;
		std::string v;
		while (p < json.size() && json[p] != '"') {
			if (json[p] == '\\' && p + 1 < json.size()) {
				char n = json[p + 1];
				switch (n) {
					case 'n':  v.push_back('\n'); p += 2; break;
					case 'r':  v.push_back('\r'); p += 2; break;
					case 't':  v.push_back('\t'); p += 2; break;
					case '"':  v.push_back('"');  p += 2; break;
					case '\\': v.push_back('\\'); p += 2; break;
					case '/':  v.push_back('/');  p += 2; break;
					case 'u':
						if (p + 5 < json.size()) {
							unsigned cp = 0;
							for (int k = 0; k < 4; ++k) {
								char h = json[p + 2 + k];
								cp <<= 4;
								if (h >= '0' && h <= '9') cp |= (h - '0');
								else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
								else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
							}
							// Encode as UTF-8 (skip surrogate pairs — rare for chat text).
							if (cp < 0x80) v.push_back((char)cp);
							else if (cp < 0x800) {
								v.push_back((char)(0xC0 | (cp >> 6)));
								v.push_back((char)(0x80 | (cp & 0x3F)));
							} else {
								v.push_back((char)(0xE0 | (cp >> 12)));
								v.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
								v.push_back((char)(0x80 | (cp & 0x3F)));
							}
							p += 6;
						} else p = json.size();
						break;
					default: v.push_back(n); p += 2; break;
				}
			} else {
				v.push_back(json[p++]);
			}
		}
		out = std::move(v);
		return true;
	}
	return false;
}

std::string buildChatBody(const std::vector<ChatMessage>& messages,
                          float temperature, int maxTokens) {
	std::string body = "{\"stream\":true,\"temperature\":";
	char tmp[64];
	snprintf(tmp, sizeof(tmp), "%.3f", temperature);
	body += tmp;
	if (maxTokens > 0) {
		snprintf(tmp, sizeof(tmp), ",\"max_tokens\":%d", maxTokens);
		body += tmp;
	}
	body += ",\"messages\":[";
	for (size_t i = 0; i < messages.size(); ++i) {
		if (i) body += ",";
		body += "{\"role\":\"";
		jsonEscapeInto(body, messages[i].role);
		body += "\",\"content\":\"";
		jsonEscapeInto(body, messages[i].content);
		body += "\"}";
	}
	body += "]}";
	return body;
}

} // namespace

// ---- LlmClient -------------------------------------------------------------

LlmClient::LlmClient(std::string host, int port)
	: m_host(std::move(host)), m_port(port) {
	m_worker = std::thread([this] { workerLoop(); });
}

LlmClient::~LlmClient() {
	{
		std::lock_guard<std::mutex> lk(m_mtx);
		m_shutdown = true;
	}
	m_cv.notify_all();
	// If a request is mid-flight, closing its fd unblocks the recv().
	int fd = m_activeFd.exchange(-1);
	if (fd >= 0) ::close(fd);
	if (m_worker.joinable()) m_worker.join();
}

uint64_t LlmClient::chatStream(std::vector<ChatMessage> messages,
                               float temperature, int maxTokens,
                               TokenCallback onToken, DoneCallback onDone) {
	Request r;
	r.id          = m_nextId.fetch_add(1);
	r.messages    = std::move(messages);
	r.temperature = temperature;
	r.maxTokens   = maxTokens;
	r.onToken     = std::move(onToken);
	r.onDone      = std::move(onDone);

	{
		std::lock_guard<std::mutex> lk(m_mtx);
		m_queue.push_back(std::move(r));
	}
	m_cv.notify_one();
	return r.id;
}

void LlmClient::cancel(uint64_t requestId) {
	// Drop from queue if still waiting.
	{
		std::lock_guard<std::mutex> lk(m_mtx);
		for (auto it = m_queue.begin(); it != m_queue.end(); ++it) {
			if (it->id == requestId) {
				auto cb = std::move(it->onDone);
				m_queue.erase(it);
				if (cb) cb(false, "cancelled");
				return;
			}
		}
	}
	// Active request: yank the socket out from under the read loop.
	if (m_activeId.load() == requestId) {
		int fd = m_activeFd.exchange(-1);
		if (fd >= 0) ::shutdown(fd, SHUT_RDWR);
	}
}

bool LlmClient::health(float timeoutSec) const {
	int fd = tcpConnectTimed(m_host, m_port, timeoutSec);
	if (fd < 0) return false;

	std::string req =
		"GET /health HTTP/1.1\r\n"
		"Host: " + m_host + "\r\n"
		"Connection: close\r\n"
		"\r\n";
	if (!sendAll(fd, req.data(), req.size())) { ::close(fd); return false; }

	std::string status = recvLine(fd);
	::close(fd);
	// "HTTP/1.1 200 OK\r\n"
	return status.find(" 200") != std::string::npos;
}

void LlmClient::workerLoop() {
	for (;;) {
		Request req;
		{
			std::unique_lock<std::mutex> lk(m_mtx);
			m_cv.wait(lk, [this] { return m_shutdown || !m_queue.empty(); });
			if (m_shutdown) return;
			req = std::move(m_queue.front());
			m_queue.pop_front();
		}
		runRequest(req);
	}
}

void LlmClient::runRequest(Request& req) {
	m_activeId.store(req.id);

	int fd = tcpConnectTimed(m_host, m_port, 2.0f);
	if (fd < 0) {
		m_activeId.store(0);
		if (req.onDone) req.onDone(false, "cannot reach llama-server (is `make llm_server` running?)");
		return;
	}
	m_activeFd.store(fd);

	std::string body = buildChatBody(req.messages, req.temperature, req.maxTokens);
	char hdr[512];
	int hlen = snprintf(hdr, sizeof(hdr),
		"POST /v1/chat/completions HTTP/1.1\r\n"
		"Host: %s:%d\r\n"
		"Content-Type: application/json\r\n"
		"Accept: text/event-stream\r\n"
		"Content-Length: %zu\r\n"
		"Connection: close\r\n"
		"\r\n",
		m_host.c_str(), m_port, body.size());
	if (hlen <= 0 || !sendAll(fd, hdr, (size_t)hlen) ||
	    !sendAll(fd, body.data(), body.size())) {
		int f = m_activeFd.exchange(-1); if (f >= 0) ::close(f);
		m_activeId.store(0);
		if (req.onDone) req.onDone(false, "send failed");
		return;
	}

	// Consume HTTP status + headers.
	std::string status = recvLine(fd);
	if (status.find(" 200") == std::string::npos) {
		// Drain a bit for diagnostics, then bail.
		std::string body2; char c;
		while (body2.size() < 512) {
			ssize_t n = ::recv(fd, &c, 1, 0);
			if (n <= 0) break;
			body2.push_back(c);
		}
		int f = m_activeFd.exchange(-1); if (f >= 0) ::close(f);
		m_activeId.store(0);
		std::string trimmed = status;
		while (!trimmed.empty() && (trimmed.back() == '\r' || trimmed.back() == '\n'))
			trimmed.pop_back();
		if (req.onDone) req.onDone(false, trimmed.empty() ? "no HTTP response" : trimmed);
		return;
	}
	for (;;) {
		std::string line = recvLine(fd);
		if (line.empty()) break;
		if (line == "\r\n" || line == "\n") break;
	}

	// SSE read loop. Each event is `data: {json}\n\n`.
	std::string dataAccum;
	bool sawDone = false;
	for (;;) {
		std::string line = recvLine(fd);
		if (line.empty()) break;  // connection closed

		// Strip trailing \r\n / \n.
		while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
			line.pop_back();

		if (line.empty()) {
			// Event boundary — flush accumulated data line.
			if (!dataAccum.empty()) {
				if (dataAccum == "[DONE]") { sawDone = true; break; }
				std::string content;
				if (extractJsonString(dataAccum, "content", content) && !content.empty()) {
					if (req.onToken) req.onToken(content);
				}
				dataAccum.clear();
			}
			continue;
		}

		// llama-server uses chunked Transfer-Encoding; we may see hex chunk
		// sizes interleaved between events. A line that is pure hex with no
		// colon can't be a valid SSE field, so skip it.
		if (line.find(':') == std::string::npos) continue;

		if (line.rfind("data:", 0) == 0) {
			size_t s = 5;
			if (s < line.size() && line[s] == ' ') ++s;
			if (!dataAccum.empty()) dataAccum.push_back('\n');
			dataAccum.append(line, s, std::string::npos);
		}
		// ignore `event:`, `id:`, `retry:`, comments (`:`)
	}

	int f = m_activeFd.exchange(-1); if (f >= 0) ::close(f);
	bool wasCancelled = (m_activeId.exchange(0) != req.id);
	if (req.onDone) {
		if (wasCancelled)    req.onDone(false, "cancelled");
		else if (sawDone)    req.onDone(true,  "");
		else                 req.onDone(false, "stream ended without [DONE]");
	}
}

} // namespace civcraft::llm
