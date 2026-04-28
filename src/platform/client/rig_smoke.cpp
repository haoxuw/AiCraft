// Tiny smoke test for the rig loader/registry. Reads
// src/artifacts/rigs/<ns>/*.py via RigRegistry::loadAll() and prints a
// summary per template (bone count, clip names, channel counts, key counts).
//
// Build & run:
//   cmake --build build --target solarium-rig-smoke
//   ./build/solarium-rig-smoke              # uses ./src/artifacts as root
//   ./build/solarium-rig-smoke <root>       # custom artifacts root
//
// Exit codes:
//   0 — at least one rig loaded
//   1 — zero rigs found at the given root
//   2 — bad CLI args

#include "client/rig.h"
#include "client/rig_loader.h"
#include "client/rig_registry.h"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
	std::string root = (argc >= 2) ? argv[1] : "src/artifacts";
	if (argc > 2) {
		std::fprintf(stderr, "usage: %s [artifacts-root]\n", argv[0]);
		return 2;
	}

	solarium::RigRegistry reg;
	int n = reg.loadAll(root);
	std::fprintf(stdout, "[rig-smoke] artifacts root: %s\n", root.c_str());
	std::fprintf(stdout, "[rig-smoke] loaded %d rig(s)\n", n);

	if (n == 0) {
		std::fprintf(stderr, "[rig-smoke] no rigs found — expected %s/rigs/<ns>/*.py\n",
		             root.c_str());
		return 1;
	}

	for (const auto& fqId : reg.ids()) {
		const solarium::Rig* r = reg.find(fqId);
		if (!r) continue;
		std::fprintf(stdout, "\n[rig-smoke] === %s ===\n", fqId.c_str());
		std::fprintf(stdout, "  id        : %s\n", r->id.c_str());
		std::fprintf(stdout, "  bones     : %zu\n", r->bones.size());
		for (const auto& b : r->bones) {
			std::fprintf(stdout, "    %-16s parent=%-12s pos=(%.3f, %.3f, %.3f)\n",
			             b.name.c_str(),
			             b.parent.empty() ? "-" : b.parent.c_str(),
			             b.defaultPos.x, b.defaultPos.y, b.defaultPos.z);
		}
		std::fprintf(stdout, "  clips     : %zu\n", r->clips.size());
		for (const auto& [name, clip] : r->clips) {
			size_t totalKeys = 0;
			for (const auto& ch : clip.channels) totalKeys += ch.keys.size();
			std::fprintf(stdout, "    %-16s duration=%.2fs  channels=%zu  keys=%zu\n",
			             name.c_str(), clip.duration,
			             clip.channels.size(), totalKeys);
		}

		// Spot-check eval: pick the first channel of the first clip and
		// sample at a few times. Verifies the lerp wraps cleanly.
		if (!r->clips.empty()) {
			const auto& [clipName, clip] = *r->clips.begin();
			if (!clip.channels.empty()) {
				const auto& ch = clip.channels.front();
				std::fprintf(stdout, "  sample    : clip=%s channel.bone=%s\n",
				             clipName.c_str(), ch.boneName.c_str());
				for (float t : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
					float deg = solarium::evalKeyframeChannel(
						ch, t * clip.duration, clip.duration);
					std::fprintf(stdout, "    t=%.2f → %+7.2f deg\n", t, deg);
				}
			}
		}
	}

	return 0;
}
