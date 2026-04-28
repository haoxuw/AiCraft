#include "server/voxel_earth/landuse.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace solarium::voxel_earth {

namespace {

// Find `"key"` in src starting from pos; return the index of the colon
// after it, or std::string::npos.
size_t find_key(const std::string& src, const char* key, size_t pos = 0) {
	std::string needle = "\"";
	needle += key;
	needle += "\"";
	const size_t k = src.find(needle, pos);
	if (k == std::string::npos) return std::string::npos;
	const size_t c = src.find(':', k + needle.size());
	return c;
}

bool parse_int(const std::string& src, const char* key, int32_t& out) {
	const size_t c = find_key(src, key);
	if (c == std::string::npos) return false;
	size_t i = c + 1;
	while (i < src.size() && std::isspace(static_cast<unsigned char>(src[i]))) ++i;
	int sign = 1;
	if (i < src.size() && src[i] == '-') { sign = -1; ++i; }
	int64_t v = 0;
	bool any = false;
	while (i < src.size() && std::isdigit(static_cast<unsigned char>(src[i]))) {
		v = v * 10 + (src[i] - '0');
		++i; any = true;
	}
	if (!any) return false;
	out = static_cast<int32_t>(sign * v);
	return true;
}

bool parse_string(const std::string& src, const char* key, std::string& out) {
	const size_t c = find_key(src, key);
	if (c == std::string::npos) return false;
	size_t i = src.find('"', c + 1);
	if (i == std::string::npos) return false;
	const size_t e = src.find('"', i + 1);
	if (e == std::string::npos) return false;
	out.assign(src, i + 1, e - i - 1);
	return true;
}

// Standard base64 decode; ignores whitespace; returns false on bad input.
bool b64_decode(const std::string& in, std::vector<uint8_t>& out) {
	out.clear();
	out.reserve(in.size() * 3 / 4);
	int buf = 0, bits = 0;
	for (char c : in) {
		if (std::isspace(static_cast<unsigned char>(c))) continue;
		if (c == '=') break;
		int v = -1;
		if (c >= 'A' && c <= 'Z') v = c - 'A';
		else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
		else if (c >= '0' && c <= '9') v = c - '0' + 52;
		else if (c == '+') v = 62;
		else if (c == '/') v = 63;
		if (v < 0) return false;
		buf = (buf << 6) | v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out.push_back(static_cast<uint8_t>((buf >> bits) & 0xFF));
		}
	}
	return true;
}

}  // namespace

bool read_landuse(const std::string& path, LanduseGrid& grid, std::string* error) {
	grid = {};
	std::ifstream f(path);
	if (!f) {
		if (error) *error = "cannot open " + path;
		return false;
	}
	std::ostringstream ss;
	ss << f.rdbuf();
	const std::string src = ss.str();

	int32_t version = 0;
	parse_int(src, "version", version);
	if (version != 1) {
		if (error) *error = "unsupported landuse.json version";
		return false;
	}
	int32_t chunk_size = 16;
	parse_int(src, "chunk_size_blocks", chunk_size);
	int32_t ox = 0, oz = 0, nx = 0, nz = 0;
	parse_int(src, "origin_x_blocks", ox);
	parse_int(src, "origin_z_blocks", oz);
	parse_int(src, "nx_chunks", nx);
	parse_int(src, "nz_chunks", nz);
	if (nx <= 0 || nz <= 0) {
		if (error) *error = "missing or invalid nx_chunks/nz_chunks";
		return false;
	}
	std::string b64;
	if (!parse_string(src, "data_b64", b64)) {
		if (error) *error = "missing data_b64";
		return false;
	}
	std::vector<uint8_t> raw;
	if (!b64_decode(b64, raw)) {
		if (error) *error = "data_b64 decode failed";
		return false;
	}
	const size_t expected = static_cast<size_t>(nx) * static_cast<size_t>(nz);
	if (raw.size() != expected) {
		if (error) {
			char buf[160];
			std::snprintf(buf, sizeof(buf),
			              "data_b64 size %zu != nx*nz=%zu", raw.size(), expected);
			*error = buf;
		}
		return false;
	}
	grid.origin_x_blocks   = ox;
	grid.origin_z_blocks   = oz;
	grid.chunk_size_blocks = chunk_size;
	grid.nx_chunks         = nx;
	grid.nz_chunks         = nz;
	grid.data              = std::move(raw);
	return true;
}

}  // namespace solarium::voxel_earth
