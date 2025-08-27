/*
Code below was generated mostly by GPT.

The goal I'm after is to have an infinite function
that operates on basic integer/rational numbers (even
complex ones built from Z+Z*i) and computes the zeta
function.

*/

#include <complex>
#include <iostream>
#include <limits>
#include <cmath>

using cd = std::complex<double>;
constexpr double PI = 3.141592653589793238462643383279502884;
constexpr double EULER_GAMMA = 0.577215664901532860606512090082402431; // γ

// ---------------------- Series primitives (no std::sin/exp) ----------------------
cd exp_series(cd z, double eps = 1e-14, std::size_t max_terms = 1000000) {
    cd term = 1.0, sum = 1.0;
    for (std::size_t k = 1; k < max_terms; ++k) {
        term *= z / (double)k;
        sum += term;
        if (std::abs(term) < eps) break;
    }
    return sum;
}

cd sin_series(cd z, double eps = 1e-14, std::size_t max_terms = 1000000) {
    cd term = z, sum = z; // k=0 term
    for (std::size_t k = 1; k < max_terms; ++k) {
        // term_k = term_{k-1} * ( - z^2 / ( (2k)(2k+1) ) )
        term *= -(z*z) / cd((double)(2*k*(2*k+1)), 0.0);
        sum += term;
        if (std::abs(term) < eps) break;
    }
    return sum;
}

// ---------------------- Gamma via Weierstrass product (+ reflection) -------------
// 1/Gamma(z) = z * e^{γ z} * Π_{n=1..∞} (1 + z/n) * e^{-z/n}
cd gamma_weierstrass(cd z, double eps = 1e-14, std::size_t max_n = 1000000) {
    // Handle poles approximately: the product will diverge; caller should avoid exact poles.
    cd inv = z * exp_series(EULER_GAMMA * z);
    cd prod = 1.0;
    for (std::size_t n = 1; n <= max_n; ++n) {
        cd a = 1.0 + z / (double)n;
        cd b = exp_series(-z / (double)n);
        cd term = a * b;              // factor_n
        prod *= term;
        // Convergence check: factors approach 1; stop when |term-1| small for a while.
        if (std::abs(term - cd(1.0,0.0)) < eps && n > 10) break;
    }
    cd inv_gamma = inv * prod;
    return cd(1.0,0.0) / inv_gamma;
}

// Reflection for general z; prefers Weierstrass on the right half-plane.
cd gamma_complex(cd z, double eps = 1e-14) {
    if (z.real() > 0.0) return gamma_weierstrass(z, eps);
    // Γ(z) = π / (sin(π z) * Γ(1 - z))
    cd piz = PI * z;
    cd sin_piz = sin_series(piz, eps);
    cd g1mz = gamma_weierstrass(cd(1.0,0.0) - z, eps);
    return (PI) / (sin_piz * g1mz);
}

// ---------------------- Zeta with minimal machinery ------------------------------
cd zeta(cd s, double eps = 1e-12, std::size_t max_iters = 1000000) {
    // // Simple pole at s = 1
    // if (std::abs(s - cd(1.0,0.0)) < 1e-15)
    //     return cd(std::numeric_limits<double>::infinity(), 0.0);

    // // Trivial zeros at negative even integers (helpful exactness)
    // if (std::abs(s.imag()) < 1e-14) {
    //     double r = s.real();
    //     double k = -r/2.0, kr = std::round(k);
    //     if (std::abs(k - kr) < 1e-12 && kr >= 1.0) return cd(0.0,0.0);
    // }

    // Re(s) > 0: alternating Dirichlet series (Dirichlet eta)
    if (s.real() > 0.0) {
        cd sum = 0.0;
        for (std::size_t n = 1;; ++n) {
            // term = (-1)^{n-1} / n^s = (-1)^{n-1} * exp( -s * ln n )
            double ln_n = std::log((double)n);          // real ln
            cd term = ((n & 1) ? 1.0 : -1.0) * exp_series(-s * ln_n, eps);
            sum += term;
            if (std::abs(term) < eps || n > max_iters) break;
        }
        // denom = 1 - 2^{1-s} = 1 - exp((1-s) ln 2)
        cd denom = cd(1.0,0.0) - exp_series((cd(1.0,0.0) - s) * std::log(2.0), eps);
        return sum / denom;
    }

    // Re(s) ≤ 0: functional equation
    cd two_pow_s   = exp_series(s * std::log(2.0), eps);
    cd pi_pow_s_1  = exp_series((s - cd(1.0,0.0)) * std::log(PI), eps);
    cd sin_factor  = sin_series( (PI/2.0) * s, eps );
    cd gamma_part  = gamma_complex(cd(1.0,0.0) - s, eps);

    cd factor = two_pow_s * pi_pow_s_1 * sin_factor * gamma_part;
    return factor * zeta(cd(1.0,0.0) - s, eps, max_iters);
}

// ---------------------- Demo / CLI -----------------------------------------------
int main() {
    std::cout.setf(std::ios::fixed); std::cout.precision(15);
    double sr, si;
    std::cout << "Enter s as 'real imag': ";
    if (!(std::cin >> sr >> si)) return 0;
    cd s(sr, si);
    cd val = zeta(s);
    std::cout << "zeta(" << sr << " + " << si << "i) = "
              << val.real() << " + " << val.imag() << "i\n";
    return 0;
}
