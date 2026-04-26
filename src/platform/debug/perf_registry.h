#pragma once

// Lightweight perf instrumentation — bucketed histograms + counters + scoped
// timers, plus a singleton Registry that accumulates them by name.
//
// ── Why bucketed, not raw samples ──────────────────────────────────────────
// Long play sessions (10s of minutes, 60 Hz render + 60 Hz tick + ~10 AI
// phases) generate ~1M samples/minute. A fixed-size log-linear histogram
// bounds memory at ~2 KB per named metric regardless of session length and
// lets percentile queries run in O(buckets), not O(n log n).
//
// ── Bucket layout ───────────────────────────────────────────────────────────
//   • Linear 0..10 ms in 100 buckets (0.1 ms resolution) — most sub-frame
//     phases live here, and we care about fine detail at the low end.
//   • Log-2 from 10 ms to ~10 s in 156 buckets (~52 per decade) — covers
//     frame stalls, rare pauses, and anything headed toward seconds.
//   • 256 buckets total. Overflow lands in the last bucket.
//
// ── Zero-cost in production ────────────────────────────────────────────────
// `ScopedTimer` and the PERF_* macros collapse to a no-op `do{}while(0)` when
// SOLARIUM_PERF is not defined. The Registry types remain compilable so
// callers can still reference the header unconditionally (e.g. for a dump
// function) but no hot-path cost is incurred.

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace solarium::perf {

class Histogram {
public:
	void record(double ms) {
		int idx = bucketOf(ms);
		m_buckets[idx]++;
		if (m_count == 0) {
			m_min = m_max = ms;
		} else {
			if (ms < m_min) m_min = ms;
			if (ms > m_max) m_max = ms;
		}
		m_count++;
		m_sum += ms;
	}

	uint64_t count() const { return m_count; }
	double   avg()   const { return m_count ? m_sum / (double)m_count : 0.0; }
	double   min()   const { return m_count ? m_min : 0.0; }
	double   max()   const { return m_count ? m_max : 0.0; }

	// p in [0, 1]. p=0.5 → median. Uses bucket midpoints — for sub-10 ms
	// values the resolution is 0.1 ms; above that it's log-2 spaced (~1.5%
	// relative error), which is plenty for "worst frame was 42 ms" reports.
	double percentile(double p) const {
		if (m_count == 0) return 0.0;
		uint64_t target = (uint64_t)((double)m_count * p);
		if (target >= m_count) target = m_count - 1;
		uint64_t cum = 0;
		for (int i = 0; i < kBuckets; i++) {
			cum += m_buckets[i];
			if (cum > target) return bucketMidpoint(i);
		}
		return m_max;
	}

	uint64_t countOver(double thresholdMs) const {
		int from = bucketOf(thresholdMs);
		uint64_t sum = 0;
		for (int i = from; i < kBuckets; i++) sum += m_buckets[i];
		return sum;
	}

private:
	static constexpr int    kBuckets           = 256;
	static constexpr double kLinCap            = 10.0;   // first 100 buckets: 0.1ms each
	static constexpr int    kLinBuckets        = 100;
	static constexpr double kLog2TenMs         = 3.321928;
	static constexpr double kLogBucketsPerOct  = 15.6;   // ~156 buckets across ~10 octaves (10ms→10s)

	static int bucketOf(double ms) {
		if (ms <= 0.0) return 0;
		if (ms < kLinCap) {
			int i = (int)(ms * 10.0);
			if (i >= kLinBuckets) i = kLinBuckets - 1;
			return i;
		}
		double l = std::log2(ms) - kLog2TenMs;
		int i = kLinBuckets + (int)(l * kLogBucketsPerOct);
		if (i < kLinBuckets) i = kLinBuckets;
		if (i >= kBuckets)   i = kBuckets - 1;
		return i;
	}

	static double bucketMidpoint(int i) {
		if (i < kLinBuckets) return (i + 0.5) * 0.1;
		double l = kLog2TenMs + (i - kLinBuckets + 0.5) / kLogBucketsPerOct;
		return std::pow(2.0, l);
	}

	uint64_t m_buckets[kBuckets] = {};
	uint64_t m_count = 0;
	double   m_sum = 0.0;
	double   m_min = 0.0, m_max = 0.0;
};

class Counter {
public:
	void     inc(uint64_t n = 1) { m_val += n; }
	uint64_t value() const       { return m_val; }
private:
	uint64_t m_val = 0;
};

// Process-wide named registry of histograms and counters. Single-threaded
// by default (the server tick loop and the client render loop are both
// single-threaded). If a future caller records from a worker thread, wrap
// the relevant histogram in its own mutex at the call site.
class Registry {
public:
	static Registry& instance() {
		static Registry r;
		return r;
	}

	Histogram& histogram(const char* name) { return m_histograms[name]; }
	Counter&   counter  (const char* name) { return m_counters  [name]; }

	const std::map<std::string, Histogram>& histograms() const { return m_histograms; }
	const std::map<std::string, Counter>&   counters()   const { return m_counters;   }

	void reset() {
		m_histograms.clear();
		m_counters.clear();
	}

private:
	std::map<std::string, Histogram> m_histograms;
	std::map<std::string, Counter>   m_counters;
};

// Format the registry as a structured human-readable block. The caller chooses
// a heading (e.g. "SOLARIUM CLIENT PERF" vs "SERVER PERF") and an elapsed
// time so we can print "frames/sec avg" etc.
//
// Output layout:
//   ── <title> ──
//   <metric>           count  avg  min  max  p50  p95  p99  p99.9
//   ...
//   counters:
//   <name> = <value>
inline std::string formatSummary(const char* title, double elapsedSec) {
	const auto& reg = Registry::instance();
	std::string out;
	char line[512];

	std::snprintf(line, sizeof(line), "── %s ── (elapsed %.1fs)\n", title, elapsedSec);
	out += line;

	if (!reg.histograms().empty()) {
		// Unit lives in the metric suffix (_ms, _count, _bytes). The column
		// header stays unit-agnostic so mixed-unit dumps don't mislabel.
		std::snprintf(line, sizeof(line),
			"%-32s %8s %10s %10s %10s %10s %10s %10s %10s\n",
			"metric", "samples", "avg", "min", "max",
			"p50", "p95", "p99", "p99.9");
		out += line;

		for (const auto& [name, h] : reg.histograms()) {
			if (h.count() == 0) continue;
			std::snprintf(line, sizeof(line),
				"%-32s %8llu %10.2f %10.2f %10.2f %10.2f %10.2f %10.2f %10.2f\n",
				name.c_str(),
				(unsigned long long)h.count(),
				h.avg(), h.min(), h.max(),
				h.percentile(0.50),
				h.percentile(0.95),
				h.percentile(0.99),
				h.percentile(0.999));
			out += line;
		}
	}

	if (!reg.counters().empty()) {
		out += "counters:\n";
		for (const auto& [name, c] : reg.counters()) {
			std::snprintf(line, sizeof(line), "  %-32s %llu\n",
				name.c_str(), (unsigned long long)c.value());
			out += line;
		}
	}
	return out;
}

// Find the metric with the highest p99 among a given set of names — useful
// for "which function was the bottleneck" one-liners.
inline std::pair<std::string, double> topByP99(
		const std::vector<const char*>& candidates) {
	const auto& reg = Registry::instance();
	std::string best;
	double bestV = -1.0;
	for (const char* n : candidates) {
		auto it = reg.histograms().find(n);
		if (it == reg.histograms().end() || it->second.count() == 0) continue;
		double v = it->second.percentile(0.99);
		if (v > bestV) { bestV = v; best = n; }
	}
	return { best, bestV };
}

#ifdef SOLARIUM_PERF

// RAII wall-clock sampler. On scope exit, records elapsed ms into a named
// histogram. Pointer storage + indirection is deliberate: the Registry
// survives for process lifetime, so a raw Histogram& is safe.
class ScopedTimer {
public:
	explicit ScopedTimer(const char* name)
	  : m_h(Registry::instance().histogram(name)),
	    m_start(std::chrono::steady_clock::now()) {}
	~ScopedTimer() {
		double ms = std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - m_start).count();
		m_h.record(ms);
	}
	ScopedTimer(const ScopedTimer&)            = delete;
	ScopedTimer& operator=(const ScopedTimer&) = delete;
private:
	Histogram& m_h;
	std::chrono::steady_clock::time_point m_start;
};

#else // !SOLARIUM_PERF

// Prod builds: trivial no-op so headers remain self-consistent.
class ScopedTimer {
public:
	explicit ScopedTimer(const char*) {}
};

#endif

} // namespace solarium::perf

// ── Macros — use these at call sites so prod builds stay zero-cost ─────────
#ifdef SOLARIUM_PERF
  #define SOLARIUM_PERF_CAT_(a,b) a##b
  #define SOLARIUM_PERF_CAT(a,b)  SOLARIUM_PERF_CAT_(a,b)
  #define PERF_SCOPE(name) \
    ::solarium::perf::ScopedTimer SOLARIUM_PERF_CAT(_civ_pt_, __LINE__)(name)
  #define PERF_RECORD_MS(name, ms) \
    ::solarium::perf::Registry::instance().histogram(name).record(ms)
  #define PERF_COUNT(name) \
    ::solarium::perf::Registry::instance().counter(name).inc()
  #define PERF_COUNT_BY(name, n) \
    ::solarium::perf::Registry::instance().counter(name).inc(n)
#else
  #define PERF_SCOPE(name)         do { (void)sizeof(name); } while(0)
  #define PERF_RECORD_MS(name, ms) do { (void)sizeof(name); (void)sizeof(ms); } while(0)
  #define PERF_COUNT(name)         do { (void)sizeof(name); } while(0)
  #define PERF_COUNT_BY(name, n)   do { (void)sizeof(name); (void)sizeof(n); } while(0)
#endif
