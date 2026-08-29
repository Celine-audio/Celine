#pragma once

#include <vector>

namespace CelineEngine
{
    //==========================================================================
    /**
        A small dense LU solver with partial pivoting, sized once and then
        reused forever without allocating.

        Circuit solves G·V = I every sample, and once per Newton iteration when
        nonlinear devices are involved. All storage is allocated in resize(),
        from Circuit::prepare() on the message thread; every method afterwards is
        allocation-free.

        Two phases, so the linear case skips most of the work:

            std::copy (stampedMatrix, ..., solver.data());
            solver.factorise();      // O(n^3), only when values change
            solver.solveInPlace (b); // O(n^2), every sample

        Double precision on purpose: a circuit mixes conductances over many
        orders of magnitude -- gmin at 1e-12 S, a 1M resistor at 1e-6 S, a
        conducting diode at ~1e-1 S -- and float's seven digits are not enough
        headroom once diodes are in the matrix. Only the solve is double; the
        audio in and out stays float.
    */
    class DenseSolver
    {
       public:
        /** Allocates for an n x n system. Call from prepare(), never from the audio thread. */
        void resize(int n);

        int size() const noexcept { return dimension; }

        /** Raw matrix storage. Callers stamp a full system elsewhere and bulk-copy
            it in, which is why there's no element accessor here: every caller
            already has its own matrix to build, and a second stamping path would
            be one more thing to keep in step. */
        double* data() noexcept { return matrix.data(); }
        const double* data() const noexcept { return matrix.data(); }

        /** In-place LU decomposition with partial pivoting.
            Returns false if the matrix is singular, in which case the stored
            factorisation must not be used. */
        bool factorise() noexcept;

        /** Solves for the right-hand side using the factorisation from the last
            successful factorise() call. `rhs` (n elements) is overwritten with
            the solution. */
        void solveInPlace(double* rhs) const noexcept;

       private:
        int dimension = 0;
        std::vector<double> matrix;      // n x n, row-major; holds L and U after factorise()
        std::vector<int> rowSwaps;       // pivot row chosen at each elimination step
        std::vector<double> invDiagonal; // 1/U(k,k), so back substitution multiplies
    };
} // namespace CelineEngine
