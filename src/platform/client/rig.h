#pragma once

// Rig template data — bone hierarchy + keyframe clips. Sibling to box_model.h.
//
// A `Rig` is a *shared template* (humanoid, quadruped, etc.) referenced by
// any number of BoxModels via `BoxModel.rigId`. Individual `BodyPart`s name
// which bone they follow via `BodyPart.bone`.
//
// Scope (Phase 1, see docs/MODEL_PIPELINE.md): pure data + a keyframe-eval
// helper. The actual transform composition in box_model_flatten.h and the
// rig-file loader land in subsequent commits — keeping those out of this
// header lets the data model land first without altering existing render
// behavior. Models with no `rigId` continue to use the sin-driver
// AnimClip path unchanged.

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace solarium {

struct Bone {
	std::string name;
	std::string parent;       // empty = root
	glm::vec3   defaultPos = {0, 0, 0};   // local position relative to parent
};

// One rotation keyframe within a channel — angle in degrees around the
// channel's axis at time `t` (seconds, in [0, KeyframeClip::duration]).
struct KeyframeRot {
	float t   = 0.0f;
	float deg = 0.0f;
};

// All keys for a single bone within a single clip. Rotation only; we don't
// keyframe translation (bones translate via parent chain only) or scale.
struct KeyframeChannel {
	std::string  boneName;
	glm::vec3    axis = {1, 0, 0};
	std::vector<KeyframeRot> keys;        // sorted ascending by t
};

struct KeyframeClip {
	float duration = 1.0f;                // seconds; clip loops at t = duration
	std::vector<KeyframeChannel> channels;
};

struct Rig {
	std::string id;                       // e.g. "base:humanoid"
	std::vector<Bone> bones;
	std::unordered_map<std::string, KeyframeClip> clips;
};

// Linear scan: bone name → index in rig.bones. Returns -1 if missing.
// O(N) — fine for ≤ 32 bones; cache the mapping in a callsite-local
// unordered_map if lookups become hot.
inline int findBone(const Rig& rig, const std::string& name) {
	for (size_t i = 0; i < rig.bones.size(); ++i)
		if (rig.bones[i].name == name) return (int)i;
	return -1;
}

// Lerp between adjacent keys at time `t`. Loops modulo duration. Returns
// degrees. O(N) over channel keys; fine for short clips (< 32 keys).
inline float evalKeyframeChannel(const KeyframeChannel& ch, float t,
                                  float duration) {
	if (ch.keys.empty()) return 0.0f;
	if (ch.keys.size() == 1) return ch.keys[0].deg;
	if (duration <= 0.0f) duration = 1.0f;

	// Wrap t into [0, duration).
	float tw = t - std::floor(t / duration) * duration;

	// Find the segment [k0, k1] that contains tw. Keys are sorted.
	size_t i = 0;
	while (i + 1 < ch.keys.size() && ch.keys[i + 1].t <= tw) ++i;

	if (i + 1 >= ch.keys.size()) {
		// tw is past the last key — interpolate from last key to first key
		// at duration (closes the loop).
		const KeyframeRot& k0 = ch.keys.back();
		const KeyframeRot& k1 = ch.keys.front();
		float span = duration - k0.t + k1.t;
		if (span <= 0.0f) return k0.deg;
		float u = (tw - k0.t) / span;
		return k0.deg + (k1.deg - k0.deg) * u;
	}

	const KeyframeRot& k0 = ch.keys[i];
	const KeyframeRot& k1 = ch.keys[i + 1];
	float span = k1.t - k0.t;
	if (span <= 0.0f) return k0.deg;
	float u = (tw - k0.t) / span;
	return k0.deg + (k1.deg - k0.deg) * u;
}

// Compute world-from-bone-origin transforms for every bone, given an active
// clip name and time. Returns one mat4 per bone, indexed parallel to
// rig.bones. Bones are processed in declaration order; **parents must
// appear before children** in the rig file (humanoid.py respects this).
//
// If `clipName` doesn't match any clip, every bone gets only its
// translate(defaultPos) — i.e. the rest pose. Bones whose `parent` doesn't
// resolve to an earlier bone are treated as roots (silent fallback so a
// malformed rig still renders).
//
// Returns an empty vector if the rig has no bones.
inline std::vector<glm::mat4> computeRigPose(const Rig& rig,
                                              const std::string& clipName,
                                              float time) {
	std::vector<glm::mat4> pose;
	pose.reserve(rig.bones.size());

	const KeyframeClip* clip = nullptr;
	if (auto it = rig.clips.find(clipName); it != rig.clips.end())
		clip = &it->second;

	// Single forward pass; parent index is found by name lookup over the
	// already-processed prefix. O(N²) worst case in bone count, fine at 16.
	for (size_t i = 0; i < rig.bones.size(); ++i) {
		const Bone& b = rig.bones[i];

		glm::mat4 local = glm::translate(glm::mat4(1.0f), b.defaultPos);
		if (clip) {
			for (const auto& ch : clip->channels) {
				if (ch.boneName != b.name) continue;
				float deg = evalKeyframeChannel(ch, time, clip->duration);
				if (std::abs(deg) < 0.001f) continue;
				local = glm::rotate(local, glm::radians(deg), ch.axis);
			}
		}

		if (b.parent.empty()) {
			pose.push_back(local);
			continue;
		}
		int parentIdx = -1;
		for (int j = 0; j < (int)i; ++j) {
			if (rig.bones[j].name == b.parent) { parentIdx = j; break; }
		}
		pose.push_back(parentIdx < 0 ? local : pose[parentIdx] * local);
	}

	return pose;
}

} // namespace solarium
