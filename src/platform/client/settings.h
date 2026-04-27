#pragma once

// Persisted user settings — single flat-JSON file at ~/.solarium/settings.json.
// Loaded once at boot; every setter writes the whole file back atomically. The
// in-memory struct is the source of truth during a session; readers (audio,
// network, UI) snapshot fields directly. Threading: setters must be called on
// the main thread (no internal locks).
//
// Why a hand-rolled flat-JSON parser instead of nlohmann/json: every value
// here is a top-level scalar (number / bool / short string). Adding a 25k-LOC
// header for that is not worth the dependency cost. If we ever grow to nested
// objects (per-save mod lists), revisit then.

#include <string>

namespace solarium {

struct Settings {
	// Audio
	float master_volume   = 0.5f;   // 0..1; multiplies every emit
	float music_volume    = 0.5f;   // 0..1
	bool  music_enabled   = true;   // false = no track plays
	bool  footsteps_muted = true;   // user asked for footsteps off by default
	bool  effects_muted   = false;  // master switch for non-music SFX

	// Network
	bool  lan_visible     = false;  // host: broadcast on UDP 7778

	// Sim
	float sim_speed_cap   = 1.0f;   // host: clamp sim multiplier (≥1)

	// Mods — comma-joined namespace ids that the artifact registry should
	// skip on next launch (no hot-reload). Empty = all enabled. The Mod
	// Manager UI mutates this string; ArtifactRegistry::setDisabledNamespaces
	// turns it into a vector.
	std::string disabled_mods;

	// Theme — drives the CEF page palette via CSS variables. Three preset
	// ids today: "brass" (default), "cobalt", "lichen". Other ids fall back
	// to brass at theme-resolve time. Live-applied via page reload — no
	// engine restart needed.
	std::string theme_id = "brass";

	// Path resolution: $XDG_CONFIG_HOME/solarium/settings.json,
	// falling back to $HOME/.solarium/settings.json.
	static const std::string& path();

	// Read-from-disk; missing file yields struct defaults. Never throws.
	static Settings load();

	// Atomic write: temp file → rename. Creates parent dir if missing.
	// Returns false on filesystem errors (logged to stderr).
	bool save() const;
};

} // namespace solarium
