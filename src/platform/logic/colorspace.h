#pragma once

// Perceptual colour distance for the voxel-earth block picker.
//
// Plain RGB Euclidean distance gives wildly mis-ranked nearest-blocks
// (greens/grays cluster, dark/bright blues mis-sort). The standard
// industry fix is sRGB → CIE Lab → CIEDE2000. Same metric VoxelEarth uses;
// 352-block catalogue still picks the right block at minDeltaE ≤ 10
// because the metric is linear in perceived difference.
//
// All math here is double-precision. Inputs are sRGB bytes.

#include <cmath>
#include <cstdint>

namespace solarium {

struct Lab { double L, a, b; };

namespace detail {

inline double srgb_to_linear(double n) {
	return (n > 0.04045) ? std::pow((n + 0.055) / 1.055, 2.4) : n / 12.92;
}

inline double f_xyz(double n) {
	return (n > 0.008856) ? std::cbrt(n) : (7.787 * n) + (16.0 / 116.0);
}

}  // namespace detail

inline Lab rgbToLab(uint8_t r8, uint8_t g8, uint8_t b8) {
	const double r = detail::srgb_to_linear(r8 / 255.0);
	const double g = detail::srgb_to_linear(g8 / 255.0);
	const double b = detail::srgb_to_linear(b8 / 255.0);

	// Observer = 2°, Illuminant = D65.
	const double X = (r * 0.4124564 + g * 0.3575761 + b * 0.1804375) / 0.95047;
	const double Y = (r * 0.2126729 + g * 0.7151522 + b * 0.0721750) / 1.00000;
	const double Z = (r * 0.0193339 + g * 0.1191920 + b * 0.9503041) / 1.08883;

	const double fx = detail::f_xyz(X);
	const double fy = detail::f_xyz(Y);
	const double fz = detail::f_xyz(Z);
	return Lab{
		116.0 * fy - 16.0,
		500.0 * (fx - fy),
		200.0 * (fy - fz),
	};
}

// CIEDE2000 ΔE — perceptual distance. Output: ~10 = "just noticeable",
// ~30 = "different colour family", ~80 = "opposite colours". Symmetric.
// Formula: https://en.wikipedia.org/wiki/Color_difference#CIEDE2000
inline double deltaE2000(Lab a, Lab b) {
	const double L1 = a.L, a1 = a.a, b1 = a.b;
	const double L2 = b.L, a2 = b.a, b2 = b.b;

	const double avgLp = (L1 + L2) * 0.5;
	const double C1 = std::sqrt(a1 * a1 + b1 * b1);
	const double C2 = std::sqrt(a2 * a2 + b2 * b2);
	const double avgC = (C1 + C2) * 0.5;
	const double avgC7 = std::pow(avgC, 7.0);
	const double G = 0.5 * (1.0 - std::sqrt(avgC7 / (avgC7 + std::pow(25.0, 7.0))));

	const double a1p = (1.0 + G) * a1;
	const double a2p = (1.0 + G) * a2;

	const double C1p = std::sqrt(a1p * a1p + b1 * b1);
	const double C2p = std::sqrt(a2p * a2p + b2 * b2);
	const double avgCp = (C1p + C2p) * 0.5;

	auto wrap = [](double h) { return h >= 0 ? h : h + 2 * M_PI; };
	const double h1p = wrap(std::atan2(b1, a1p));
	const double h2p = wrap(std::atan2(b2, a2p));

	double avgHp;
	if (std::abs(h1p - h2p) > M_PI) avgHp = (h1p + h2p + 2 * M_PI) * 0.5;
	else                            avgHp = (h1p + h2p) * 0.5;

	const double T = 1.0
		- 0.17 * std::cos(avgHp - M_PI / 6.0)
		+ 0.24 * std::cos(2 * avgHp)
		+ 0.32 * std::cos(3 * avgHp + M_PI / 30.0)
		- 0.20 * std::cos(4 * avgHp - 63.0 * M_PI / 180.0);

	double dHp;
	if (std::abs(h2p - h1p) <= M_PI)      dHp = h2p - h1p;
	else if (h2p <= h1p)                  dHp = h2p - h1p + 2 * M_PI;
	else                                  dHp = h2p - h1p - 2 * M_PI;

	const double dLp = L2 - L1;
	const double dCp = C2p - C1p;
	const double dHpDeg = 2.0 * std::sqrt(C1p * C2p) * std::sin(dHp * 0.5);

	const double SL = 1.0 + (0.015 * (avgLp - 50.0) * (avgLp - 50.0)) /
	                  std::sqrt(20.0 + (avgLp - 50.0) * (avgLp - 50.0));
	const double SC = 1.0 + 0.045 * avgCp;
	const double SH = 1.0 + 0.015 * avgCp * T;

	const double dTheta = (M_PI / 6.0) *
		std::exp(-std::pow((avgHp - 275.0 * M_PI / 180.0) / (25.0 * M_PI / 180.0), 2));
	const double avgCp7 = std::pow(avgCp, 7.0);
	const double RC = 2.0 * std::sqrt(avgCp7 / (avgCp7 + std::pow(25.0, 7.0)));
	const double RT = -RC * std::sin(2 * dTheta);

	return std::sqrt(
		(dLp / SL) * (dLp / SL) +
		(dCp / SC) * (dCp / SC) +
		(dHpDeg / SH) * (dHpDeg / SH) +
		RT * (dCp / SC) * (dHpDeg / SH));
}

}  // namespace solarium
