#include "server/voxel_earth/voxel_palette.h"
#include "server/voxel_earth/palette.h"   // kAlphaFillStone / kAlphaFillDirt

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace solarium::voxel_earth {

namespace {

// Median-cut bucket: a contiguous slice of the working voxel array.
struct Bucket {
	uint32_t lo, hi;        // half-open [lo, hi)
	uint8_t  rmin, rmax, gmin, gmax, bmin, bmax;

	int range_axis() const {
		const int dr = rmax - rmin;
		const int dg = gmax - gmin;
		const int db = bmax - bmin;
		if (dr >= dg && dr >= db) return 0;
		if (dg >= db)             return 1;
		return 2;
	}
	int range_size() const {
		return std::max(rmax - rmin, std::max(gmax - gmin, bmax - bmin));
	}
};

// Pull (r,g,b) into a flat working array, skipping fill-sentinel voxels.
struct RGB { uint8_t r, g, b; };

void fill_bucket_extents(const RGB* a, Bucket& bk) {
	bk.rmin = bk.gmin = bk.bmin = 255;
	bk.rmax = bk.gmax = bk.bmax = 0;
	for (uint32_t i = bk.lo; i < bk.hi; ++i) {
		const RGB& v = a[i];
		if (v.r < bk.rmin) bk.rmin = v.r;
		if (v.r > bk.rmax) bk.rmax = v.r;
		if (v.g < bk.gmin) bk.gmin = v.g;
		if (v.g > bk.gmax) bk.gmax = v.g;
		if (v.b < bk.bmin) bk.bmin = v.b;
		if (v.b > bk.bmax) bk.bmax = v.b;
	}
}

}  // namespace

void VoxelPalette::build(const VoxelRegion& region) {
	// 1. Collect non-fill voxel RGBs.
	std::vector<RGB> arr;
	arr.reserve(region.voxels.size());
	for (const auto& v : region.voxels) {
		if (v.a == kAlphaFillStone || v.a == kAlphaFillDirt) continue;
		arr.push_back({v.r, v.g, v.b});
	}

	// Identity entry — passthrough tint for fill voxels.
	centroids[kIdentityIndex] = {128, 128, 128};

	// Edge case: empty or tiny region — fill remaining slots with neutral grey.
	if (arr.size() < 2) {
		for (int i = 1; i < kAppearancePaletteSize; ++i)
			centroids[i] = {128, 128, 128};
		built = true;
		return;
	}

	// 2. Median-cut: start with one bucket containing all colours, repeatedly
	// split the bucket whose RGB range is widest, until we have enough buckets
	// to fill indices 1..255 (i.e. kAppearancePaletteSize - 1 of them).
	const int target_buckets = kAppearancePaletteSize - 1;  // index 0 is identity
	std::vector<Bucket> buckets;
	buckets.reserve(target_buckets);
	{
		Bucket b{ 0, (uint32_t)arr.size(), 0,0,0,0,0,0 };
		fill_bucket_extents(arr.data(), b);
		buckets.push_back(b);
	}

	while ((int)buckets.size() < target_buckets) {
		// Find bucket with widest range AND > 1 element to split.
		int pick = -1;
		int best = -1;
		for (size_t i = 0; i < buckets.size(); ++i) {
			if (buckets[i].hi - buckets[i].lo < 2) continue;
			const int r = buckets[i].range_size();
			if (r > best) { best = r; pick = (int)i; }
		}
		if (pick < 0 || best <= 0) break;  // all buckets are singletons or zero-range

		Bucket& bk = buckets[pick];
		const int axis = bk.range_axis();
		// Sort the slice by the chosen axis.
		auto cmp = [axis](const RGB& a, const RGB& b) {
			if (axis == 0) return a.r < b.r;
			if (axis == 1) return a.g < b.g;
			return a.b < b.b;
		};
		std::nth_element(arr.begin() + bk.lo,
		                 arr.begin() + (bk.lo + (bk.hi - bk.lo) / 2),
		                 arr.begin() + bk.hi, cmp);
		const uint32_t mid = bk.lo + (bk.hi - bk.lo) / 2;
		Bucket left  { bk.lo, mid, 0,0,0,0,0,0 };
		Bucket right { mid,   bk.hi, 0,0,0,0,0,0 };
		fill_bucket_extents(arr.data(), left);
		fill_bucket_extents(arr.data(), right);
		buckets[pick] = left;
		buckets.push_back(right);
	}

	// 3. Each bucket's centroid = mean RGB of its members.
	for (size_t i = 0; i < buckets.size(); ++i) {
		const Bucket& b = buckets[i];
		uint64_t sr = 0, sg = 0, sb = 0;
		const uint32_t n = b.hi - b.lo;
		for (uint32_t j = b.lo; j < b.hi; ++j) {
			sr += arr[j].r; sg += arr[j].g; sb += arr[j].b;
		}
		centroids[i + 1] = {  // +1 because index 0 is identity
			static_cast<uint8_t>(sr / n),
			static_cast<uint8_t>(sg / n),
			static_cast<uint8_t>(sb / n),
		};
	}
	// 4. Pad any leftover indices with neutral grey if median-cut bailed early.
	for (size_t i = buckets.size() + 1; i < kAppearancePaletteSize; ++i)
		centroids[i] = {128, 128, 128};

	built = true;
	std::printf("[VoxelEarth] appearance palette built — %zu non-fill voxels, %zu buckets\n",
	            arr.size(), buckets.size());
}

uint8_t VoxelPalette::nearest(uint8_t r, uint8_t g, uint8_t b) const {
	int best_d = 0x7FFFFFFF;
	int best_i = 1;
	// Skip index 0 (identity / fill) — only return a real source-RGB centroid.
	for (int i = 1; i < kAppearancePaletteSize; ++i) {
		const int dr = (int)r - (int)centroids[i][0];
		const int dg = (int)g - (int)centroids[i][1];
		const int db = (int)b - (int)centroids[i][2];
		const int d  = dr*dr + dg*dg + db*db;
		if (d < best_d) { best_d = d; best_i = i; }
	}
	return static_cast<uint8_t>(best_i);
}

std::vector<AppearanceEntry> VoxelPalette::as_palette_for(const glm::vec3& block_base) const {
	std::vector<AppearanceEntry> out;
	out.resize(kAppearancePaletteSize);
	// Identity passthrough — fill voxels and any unset cell render natural.
	out[kIdentityIndex].tint = {1.0f, 1.0f, 1.0f};
	// Avoid divide-by-zero on degenerate base colours.
	const float br = std::max(block_base.r, 1.0f / 255.0f);
	const float bg = std::max(block_base.g, 1.0f / 255.0f);
	const float bb = std::max(block_base.b, 1.0f / 255.0f);
	for (int i = 1; i < kAppearancePaletteSize; ++i) {
		const float cr = centroids[i][0] / 255.0f;
		const float cg = centroids[i][1] / 255.0f;
		const float cb = centroids[i][2] / 255.0f;
		out[i].tint = { cr / br, cg / bg, cb / bb };
	}
	return out;
}

}  // namespace solarium::voxel_earth
