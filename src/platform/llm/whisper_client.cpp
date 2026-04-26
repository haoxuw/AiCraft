#include "llm/whisper_client.h"

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

namespace solarium::llm {

namespace {

// ---- socket helpers (copies of LlmClient's, deliberately duplicated so
// WhisperClient has no link-time dep on llm_client.cpp) ---------------------

int tcpConnectTimed(const std::string& host, int port, float timeoutSec) {
	int fd = ::socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) return -1;

	int flags = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
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
		if (w < 0 && errno == EINTR) continue;
		return false;
	}
	return true;
}

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

// Read exactly `want` bytes or fail.
bool recvExact(int fd, std::string& out, size_t want) {
	out.resize(want);
	size_t got = 0;
	while (got < want) {
		ssize_t n = ::recv(fd, out.data() + got, want - got, 0);
		if (n <= 0) { out.resize(got); return false; }
		got += (size_t)n;
	}
	return true;
}

// Minimal JSON string-extractor for the `"text": "..."` field in whisper-server
// replies. Same style as llm_client.cpp.
bool extractJsonString(const std::string& json, const std::string& key, std::string& out) {
	std::string needle = "\"" + key + "\"";
	size_t p = 0;
	while ((p = json.find(needle, p)) != std::string::npos) {
		p += needle.size();
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
					default:   v.push_back(n);    p += 2; break;
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

} // namespace

// ---- WhisperClient ---------------------------------------------------------

WhisperClient::WhisperClient(const Config& cfg) : m_cfg(cfg) {
	m_thread = std::thread([this] { workerLoop(); });
}

WhisperClient::~WhisperClient() {
	{
		std::lock_guard<std::mutex> lk(m_mtx);
		m_quit.store(true);
	}
	m_cv.notify_all();
	if (m_thread.joinable()) m_thread.join();
}

void WhisperClient::transcribe(std::vector<uint8_t> wavBytes, OnDone onDone) {
	Request r;
	r.wav    = std::move(wavBytes);
	r.onDone = std::move(onDone);
	{
		std::lock_guard<std::mutex> lk(m_mtx);
		m_queue.push_back(std::move(r));
	}
	m_cv.notify_one();
}

void WhisperClient::workerLoop() {
	for (;;) {
		Request req;
		{
			std::unique_lock<std::mutex> lk(m_mtx);
			m_cv.wait(lk, [this] { return m_quit.load() || !m_queue.empty(); });
			if (m_quit.load()) return;
			req = std::move(m_queue.front());
			m_queue.pop_front();
		}

		std::string text, err;
		bool ok = runRequest(req, &text, &err);
		if (req.onDone) req.onDone(ok, ok ? text : err);
	}
}

bool WhisperClient::runRequest(const Request& r, std::string* text, std::string* err) {
	if (r.wav.empty()) { *err = "empty audio"; return false; }

	int fd = tcpConnectTimed(m_cfg.host, m_cfg.port, 2.0f);
	if (fd < 0) {
		*err = "cannot reach whisper-server";
		return false;
	}

	// multipart/form-data body — two parts: `file` and `response_format`.
	const std::string boundary = "----SolariumBoundary7bKq9pM2";

	std::string preFile;
	preFile += "--" + boundary + "\r\n";
	preFile += "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n";
	preFile += "Content-Type: audio/wav\r\n\r\n";

	std::string postFile;
	postFile += "\r\n--" + boundary + "\r\n";
	postFile += "Content-Disposition: form-data; name=\"response_format\"\r\n\r\n";
	postFile += "json\r\n";
	postFile += "--" + boundary + "--\r\n";

	const size_t contentLen = preFile.size() + r.wav.size() + postFile.size();

	char hdr[512];
	int hlen = snprintf(hdr, sizeof(hdr),
		"POST /inference HTTP/1.1\r\n"
		"Host: %s:%d\r\n"
		"Content-Type: multipart/form-data; boundary=%s\r\n"
		"Content-Length: %zu\r\n"
		"Connection: close\r\n"
		"\r\n",
		m_cfg.host.c_str(), m_cfg.port, boundary.c_str(), contentLen);

	if (hlen <= 0 ||
	    !sendAll(fd, hdr, (size_t)hlen) ||
	    !sendAll(fd, preFile.data(), preFile.size()) ||
	    !sendAll(fd, (const char*)r.wav.data(), r.wav.size()) ||
	    !sendAll(fd, postFile.data(), postFile.size())) {
		::close(fd);
		*err = "send failed";
		return false;
	}

	// Parse HTTP status + headers. Track Content-Length so we know how much
	// body to read (whisper-server replies non-chunked for /inference).
	std::string status = recvLine(fd);
	if (status.find(" 200") == std::string::npos) {
		::close(fd);
		while (!status.empty() && (status.back() == '\r' || status.back() == '\n'))
			status.pop_back();
		*err = status.empty() ? "no HTTP response" : status;
		return false;
	}

	size_t bodyLen = 0;
	bool chunked = false;
	for (;;) {
		std::string line = recvLine(fd);
		if (line.empty() || line == "\r\n" || line == "\n") break;
		// lower-case header key for matching
		std::string lower;
		lower.reserve(line.size());
		for (char c : line) lower.push_back((char)std::tolower((unsigned char)c));
		if (lower.rfind("content-length:", 0) == 0) {
			bodyLen = (size_t)std::strtoull(lower.c_str() + 15, nullptr, 10);
		} else if (lower.rfind("transfer-encoding:", 0) == 0 &&
		           lower.find("chunked") != std::string::npos) {
			chunked = true;
		}
	}

	std::string body;
	if (chunked) {
		// Simple chunked decoder.
		for (;;) {
			std::string sizeLine = recvLine(fd);
			while (!sizeLine.empty() && (sizeLine.back() == '\r' || sizeLine.back() == '\n'))
				sizeLine.pop_back();
			if (sizeLine.empty()) { ::close(fd); *err = "bad chunk size"; return false; }
			size_t sz = (size_t)std::strtoull(sizeLine.c_str(), nullptr, 16);
			if (sz == 0) break;
			std::string chunk;
			if (!recvExact(fd, chunk, sz)) { ::close(fd); *err = "chunk truncated"; return false; }
			body += chunk;
			// Consume trailing \r\n after chunk.
			recvLine(fd);
		}
	} else if (bodyLen > 0) {
		if (!recvExact(fd, body, bodyLen)) { ::close(fd); *err = "body truncated"; return false; }
	} else {
		// No content-length and not chunked — drain until EOF.
		char buf[4096];
		for (;;) {
			ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
			if (n <= 0) break;
			body.append(buf, (size_t)n);
		}
	}
	::close(fd);

	std::string out;
	if (!extractJsonString(body, "text", out)) {
		*err = "no text field in whisper reply";
		return false;
	}
	// Trim leading/trailing whitespace that whisper often emits.
	size_t a = 0, b = out.size();
	while (a < b && (out[a] == ' ' || out[a] == '\n' || out[a] == '\r' || out[a] == '\t')) ++a;
	while (b > a && (out[b - 1] == ' ' || out[b - 1] == '\n' || out[b - 1] == '\r' || out[b - 1] == '\t')) --b;
	*text = out.substr(a, b - a);
	return true;
}

} // namespace solarium::llm
