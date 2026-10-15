// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abraham Joseph Okayli Masaryk
// SoundNuts — analytic/elliptic.hpp
// Jacobi elliptic functions + complete elliptic integral, the primitives behind
// the closed-form pendulum / Duffing solutions.
//
// CONVENTION: modulus k (NOT the parameter m = k^2). Be explicit because dysts /
// Boost / Abramowitz-Stegun disagree — mixing them silently halves/doubles periods.
//
// Header-only, std-only. K(k) via the arithmetic-geometric mean; sn/cn/dn via the
// AGM + descending-Landen scheme (Abramowitz & Stegun 16.4). Accurate to ~1e-12
// for k in [0, 1). No Boost dependency.

#pragma once

#include <cmath>
#include <limits>

namespace soundnuts::detail {

inline constexpr double kEllPi = 3.14159265358979323846264338327950288;

// Complete elliptic integral of the first kind K(k), modulus convention.
// K(0) = pi/2; K(k) -> +inf as k -> 1 (this is the separatrix period blow-up).
inline double ellint_K(double k) {
    const double m1 = 1.0 - k * k;                 // k'^2
    if (m1 <= 0.0) return std::numeric_limits<double>::infinity();
    double a = 1.0, b = std::sqrt(m1);
    for (int i = 0; i < 64; ++i) {
        const double an = 0.5 * (a + b);
        if (std::fabs(a - b) <= 1e-16 * a) { a = an; break; }
        b = std::sqrt(a * b);
        a = an;
    }
    return kEllPi / (2.0 * a);
}

// All three Jacobi functions at once (the cheap way — sn/cn/dn share the AGM).
inline void jacobi(double u, double k, double& sn, double& cn, double& dn) {
    if (k < 1e-12) {                               // degenerate: circular limit
        sn = std::sin(u); cn = std::cos(u); dn = 1.0; return;
    }
    if (k > 1.0 - 1e-12) {                         // degenerate: hyperbolic limit
        const double t = std::tanh(u), s = 1.0 / std::cosh(u);
        sn = t; cn = s; dn = s; return;
    }

    constexpr int kMaxN = 32;
    double a[kMaxN + 1], c[kMaxN + 1];
    a[0] = 1.0;
    c[0] = k;
    double b = std::sqrt(1.0 - k * k);             // b_0 = k'
    int n = 0;
    for (; n < kMaxN; ++n) {
        if (std::fabs(c[n]) < 1e-15) break;
        a[n + 1] = 0.5 * (a[n] + b);
        c[n + 1] = 0.5 * (a[n] - b);
        b = std::sqrt(a[n] * b);                   // b_{n+1}
    }

    double phi = std::ldexp(a[n] * u, n);          // phi_N = 2^n a_n u
    for (int i = n; i >= 1; --i)
        phi = 0.5 * (phi + std::asin((c[i] / a[i]) * std::sin(phi)));

    sn = std::sin(phi);
    cn = std::cos(phi);
    dn = std::sqrt(1.0 - k * k * sn * sn);         // identity: robust, k<1
}

inline double jacobi_sn(double u, double k) { double s, c, d; jacobi(u, k, s, c, d); return s; }
inline double jacobi_cn(double u, double k) { double s, c, d; jacobi(u, k, s, c, d); return c; }
inline double jacobi_dn(double u, double k) { double s, c, d; jacobi(u, k, s, c, d); return d; }

}  // namespace soundnuts::detail
