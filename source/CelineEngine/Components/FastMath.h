#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

//==============================================================================
/**
    Approximations of the three library functions this engine actually spends
    time in, and a switch to turn them on.

    Every nonlinear device is evaluated once per port per Newton iteration, and
    each evaluation costs an `exp`, often a `log1p`, and for a valve a `pow`.
    At four iterations on a six-triode preamp that is several hundred calls per
    sample, and libm's versions are correctly rounded to the last bit -- which
    is a guarantee nobody listening to a guitar amp is paying for.

    The versions here are accurate to roughly 1e-9 relative, which is four
    orders of magnitude tighter than the Newton tolerance they feed, and about
    six orders tighter than anything audible. `sqrt` is deliberately absent: it
    is a single hardware instruction already.

    **The switch is process-wide**, not per instance. Two plugin instances in
    one host share it, so the last one to set it wins. That is a real
    limitation and the alternative -- threading a flag down into every device's
    free functions, or templating the Newton loop on it -- costs more in
    complexity than the case is worth. The flag is read with a relaxed atomic
    load, which compiles to a plain load and predicts perfectly.
*/
namespace CircuitComponents
{
    namespace FastMath
    {
        /** Process-wide. Set through setEnabled() rather than written directly. */
        inline std::atomic<bool> enabled{false};

        inline void setEnabled(bool shouldUseFastMath) noexcept
        {
            enabled.store(shouldUseFastMath, std::memory_order_relaxed);
        }

        inline bool isEnabled() noexcept { return enabled.load(std::memory_order_relaxed); }

        //======================================================================
        /** exp(x), by splitting into a power of two and a polynomial.

            x*log2(e) splits into an integer k and a fraction f in [-0.5, 0.5].
            2^k is assembled by writing the exponent field directly, and 2^f is
            a seventh-order Taylor series in f -- whose next term is bounded by
            2e-9 over that interval, so that is the error. */
        inline double fastExp(double x) noexcept
        {
            if (x > 709.0)
                return std::numeric_limits<double>::infinity();

            if (x < -708.0)
                return 0.0;

            constexpr double log2e = 1.4426950408889634074;
            const double t = x * log2e;
            const double k = std::floor(t + 0.5);
            const double f = t - k;

            // Coefficients are (ln2)^n / n!, so this is exp(f * ln2) = 2^f.
            // Eight terms rather than seven: the first term dropped is bounded
            // by c9 * 0.5^9 = 2e-10, where stopping at c7 left 7e-9. One extra
            // multiply-add for a factor of thirty in accuracy is a good trade.
            constexpr double c1 = 6.9314718055994530942e-1;
            constexpr double c2 = 2.4022650695910071233e-1;
            constexpr double c3 = 5.5504108664821579953e-2;
            constexpr double c4 = 9.6181291076284771619e-3;
            constexpr double c5 = 1.3333558146428443423e-3;
            constexpr double c6 = 1.5403530393381609954e-4;
            constexpr double c7 = 1.5252733804059840280e-5;
            constexpr double c8 = 1.3215020367156590e-6;

            const double p =
                1.0
                + f * (c1 + f * (c2 + f * (c3 + f * (c4 + f * (c5 + f * (c6 + f * (c7 + f * c8)))))));

            const auto exponentBits = static_cast<uint64_t>(static_cast<int64_t>(k) + 1023) << 52;
            double twoToTheK;
            std::memcpy(&twoToTheK, &exponentBits, sizeof(twoToTheK));

            return p * twoToTheK;
        }

        /** log(x), by pulling the exponent out and expanding the mantissa.

            The mantissa is folded into [sqrt(0.5), sqrt(2)) first, which keeps
            s = (m-1)/(m+1) below 0.172 and makes the odd series in s converge
            fast enough that six terms land near 1e-11. */
        inline double fastLog(double x) noexcept
        {
            if (! (x > 0.0))
                return x == 0.0 ? -std::numeric_limits<double>::infinity()
                                : std::numeric_limits<double>::quiet_NaN();

            uint64_t bits;
            std::memcpy(&bits, &x, sizeof(bits));

            auto exponent = static_cast<int>((bits >> 52) & 0x7FFULL) - 1023;

            // Subnormals have no exponent to pull out; they never arise here,
            // but falling back keeps the function total.
            if (exponent == -1023)
                return std::log(x);

            bits = (bits & 0x000FFFFFFFFFFFFFULL) | (1023ULL << 52);
            double mantissa;
            std::memcpy(&mantissa, &bits, sizeof(mantissa));

            if (mantissa > 1.4142135623730951)
            {
                mantissa *= 0.5;
                ++exponent;
            }

            const double s = (mantissa - 1.0) / (mantissa + 1.0);
            const double s2 = s * s;

            const double series =
                2.0 * s
                * (1.0
                   + s2
                         * (1.0 / 3.0
                            + s2 * (1.0 / 5.0 + s2 * (1.0 / 7.0 + s2 * (1.0 / 9.0 + s2 * (1.0 / 11.0))))));

            constexpr double ln2 = 6.9314718055994530942e-1;
            return series + static_cast<double>(exponent) * ln2;
        }

        /** log(1+u). The series is used where 1+u would lose u's low bits. */
        inline double fastLog1p(double u) noexcept
        {
            if (u > -1.0e-4 && u < 1.0e-4)
                return u * (1.0 - u * (0.5 - u * (1.0 / 3.0)));

            return fastLog(1.0 + u);
        }

        /** pow(x, y) for x > 0, which is the only case this engine asks for --
            it is always some positive current or voltage raised to an
            exponent between 1.3 and 1.5. */
        inline double fastPow(double x, double y) noexcept
        {
            if (x <= 0.0)
                return 0.0;

            return fastExp(y * fastLog(x));
        }
    } // namespace FastMath

    //==========================================================================
    // What the device models call. One predictable branch, then either the
    // library or the approximation above.

    inline double fastOrExactExp(double x) noexcept
    {
        return FastMath::isEnabled() ? FastMath::fastExp(x) : std::exp(x);
    }

    inline double fastOrExactLog(double x) noexcept
    {
        return FastMath::isEnabled() ? FastMath::fastLog(x) : std::log(x);
    }

    inline double fastOrExactLog1p(double x) noexcept
    {
        return FastMath::isEnabled() ? FastMath::fastLog1p(x) : std::log1p(x);
    }

    inline double fastOrExactPow(double x, double y) noexcept
    {
        return FastMath::isEnabled() ? FastMath::fastPow(x, y) : std::pow(x, y);
    }
} // namespace CircuitComponents
