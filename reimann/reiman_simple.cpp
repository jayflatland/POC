#include <complex>
#include <iostream>
#include <cmath>
#include <limits>

using cd = std::complex<double>;
constexpr double PI = 3.14159265358979323846;

/*
Code below was generated mostly by GPT.

The goal I'm after is to have an infinite function
that operates on basic integer/rational numbers (even
complex ones built from Z+Z*i) and computes the zeta
function.

functions that need expansion:
 - tgamma()
 - sin()
 - abs()
 - pow()


*/

// minimal gamma for complex z via reflection and std::tgamma for real part
cd gamma_complex(cd z) {
    if (z.imag() == 0.0 && z.real() > 0.0) {
        return cd(std::tgamma(z.real()), 0.0);
    }
    // reflection formula: gamma(z) = pi / (sin(pi*z) gamma(1-z))
    return PI / (std::sin(PI * z) * gamma_complex(1.0 - z));
}

// main zeta function
cd zeta(cd s, double eps = 1e-12, std::size_t max_iters = 1000000) {
    // // pole at s=1
    // if (std::abs(s - cd(1.0,0.0)) < 1e-14) {
    //     return cd(std::numeric_limits<double>::infinity(), 0.0);
    // }

    // // trivial zeros
    // if (std::abs(s.imag()) < 1e-14) {
    //     double r = s.real();
    //     double k = -r/2.0, k_round = std::round(k);
    //     if (std::abs(k - k_round) < 1e-12 && k_round >= 1)
    //         return cd(0.0,0.0);
    // }

    // Re(s) > 0: use alternating series
    if (s.real() > 0.0) {
        cd sum = 0.0;
        std::size_t n;
        for (n = 1;; ++n) {
            cd term = ((n & 1) ? 1.0 : -1.0) / std::pow((double)n, s);
            sum += term;
            // std::cout << "term=" << term << std::endl;
            if (std::abs(term) < eps || n > max_iters) break;
        }
        std::cout << "n=" << n << std::endl;
        return sum / (1.0 - std::pow(2.0, 1.0 - s));
    }

    // Re(s) <= 0: functional equation
    cd factor = std::pow(2.0, s) * std::pow(PI, s - 1.0) * std::sin(PI * s / 2.0) * gamma_complex(1.0 - s);
    return factor * zeta(1.0 - s, eps, max_iters);
}

int main() {
    std::cout.setf(std::ios::fixed); std::cout.precision(15);
    double sr, si;
    std::cout << "Enter s as 'real imag': ";
    if (!(std::cin >> sr >> si)) return 0;
    cd s(sr, si);
    cd val = zeta(s);
    std::cout << "zeta(" << sr << " + " << si << "i) = "
              << val.real() << " + " << val.imag() << "i\n";
}
