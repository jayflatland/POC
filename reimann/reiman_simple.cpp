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

using cd = std::complex<double>;

// -------------------- Generate constants from series --------------------
// π via Gregory–Leibniz: π = 4 * Σ (-1)^n / (2n+1)
double compute_pi(std::size_t max_terms = 1000000, double eps=1e-14) {
    double sum = 0.0;
    for (std::size_t n = 0; n < max_terms; ++n) {
        double term = ((n & 1) ? -1.0 : 1.0) / (2.0*n + 1.0);
        sum += term;
        if (std::abs(term) < eps) break;
    }
    return 4.0 * sum;
}

// Euler–Mascheroni γ = lim_{n→∞} ( Σ_{k=1}^n 1/k - ln n )
// Here we approximate ln n with Taylor log_series
double log_series(double x, double eps=1e-14, std::size_t max_terms=1000000);

double compute_gamma(std::size_t N = 100000) {
    double sum = 0.0;
    for (std::size_t k = 1; k <= N; ++k) sum += 1.0 / (double)k;
    return sum - log_series((double)N);
}

// -------------------- Series primitives --------------------
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
    cd term = z, sum = z;
    for (std::size_t k = 1; k < max_terms; ++k) {
        term *= -(z*z) / cd((double)(2*k*(2*k+1)),0.0);
        sum += term;
        if (std::abs(term) < eps) break;
    }
    return sum;
}

// Natural log via Taylor expansion around 1, with argument reduction
// ln(x) = 2 * [ y + y^3/3 + y^5/5 + ... ], with y=(x-1)/(x+1)
double log_series(double x, double eps, std::size_t max_terms) {
    if (x <= 0.0) return -std::numeric_limits<double>::infinity();
    int k = 0;
    while (x > 2.0) { x /= 2.0; k++; }
    while (x < 0.5) { x *= 2.0; k--; }
    double y = (x - 1.0) / (x + 1.0);
    double ypow = y, sum = 0.0;
    for (std::size_t n = 0; n < max_terms; ++n) {
        double term = ypow / (2*n+1);
        sum += term;
        ypow *= y*y;
        if (std::abs(term) < eps) break;
    }
    return 2.0*sum + k*log_series(2.0, eps, max_terms); // ln(2) recursively
}

// -------------------- Gamma via Weierstrass --------------------
cd gamma_weierstrass(cd z, double gamma_const, double eps=1e-14, std::size_t max_n=200000) {
    cd inv = z * exp_series(gamma_const * z);
    cd prod = 1.0;
    for (std::size_t n = 1; n <= max_n; ++n) {
        cd term = (1.0 + z/(double)n) * exp_series(-z/(double)n);
        prod *= term;
        if (std::abs(term - cd(1.0,0.0)) < eps && n > 10) break;
    }
    return cd(1.0,0.0) / (inv * prod);
}

cd gamma_complex(cd z, double pi_const, double gamma_const) {
    if (z.real() > 0.0) return gamma_weierstrass(z, gamma_const);
    return pi_const / (sin_series(pi_const*z) * gamma_weierstrass(cd(1.0,0.0)-z, gamma_const));
}

// -------------------- Zeta function --------------------
cd zeta(cd s, double pi_const, double gamma_const, double eps=1e-12, std::size_t max_iters=100000) {
    if (std::abs(s - cd(1.0,0.0)) < 1e-14)
        return cd(std::numeric_limits<double>::infinity(),0.0);

    if (std::abs(s.imag()) < 1e-14) {
        double r = s.real();
        double k = -r/2.0, kr = std::round(k);
        if (std::abs(k - kr) < 1e-12 && kr >= 1) return cd(0.0,0.0);
    }

    if (s.real() > 0.0) {
        cd sum = 0.0;
        for (std::size_t n = 1;; ++n) {
            double ln_n = log_series((double)n);
            cd term = ((n & 1) ? 1.0 : -1.0) * exp_series(-s*ln_n);
            sum += term;
            if (std::abs(term) < eps || n > max_iters) break;
        }
        cd denom = 1.0 - exp_series((cd(1.0)-s)*log_series(2.0));
        return sum / denom;
    }

    cd two_pow_s  = exp_series(s*log_series(2.0));
    cd pi_pow_s1  = exp_series((s-cd(1.0))*log_series(pi_const));
    cd sin_fac    = sin_series((pi_const/2.0)*s);
    cd gamma_part = gamma_complex(cd(1.0)-s, pi_const, gamma_const);

    return two_pow_s * pi_pow_s1 * sin_fac * gamma_part * zeta(cd(1.0)-s, pi_const, gamma_const, eps, max_iters);
}

// -------------------- Demo --------------------
int main() {
    std::cout.setf(std::ios::fixed); std::cout.precision(15);
    double PI = compute_pi(200000);
    double GAMMA = compute_gamma(200000);
    std::cout << "Computed constants:\n";
    std::cout << "PI ≈ " << PI << "\n";
    std::cout << "Euler–Mascheroni γ ≈ " << GAMMA << "\n";

    double sr, si;
    std::cout << "Enter s as 'real imag': ";
    if (!(std::cin >> sr >> si)) return 0;
    cd s(sr, si);
    cd val = zeta(s, PI, GAMMA);
    std::cout << "zeta(" << sr << " + " << si << "i) = "
              << val.real() << " + " << val.imag() << "i\n";
}
