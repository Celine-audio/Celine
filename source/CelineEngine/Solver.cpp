#include "Solver.h"

#include <algorithm>
#include <cmath>

namespace CelineEngine
{
    void DenseSolver::resize(int n)
    {
        dimension = n;
        matrix.assign(static_cast<size_t>(n * n), 0.0);
        rowSwaps.assign(static_cast<size_t>(n), 0);
        invDiagonal.assign(static_cast<size_t>(n), 0.0);
    }

    bool DenseSolver::factorise() noexcept
    {
        const int n = dimension;
        double* a = matrix.data();

        for (int k = 0; k < n; ++k)
        {
            // Partial pivoting: swap in the row with the largest element in this column.
            int pivotRow = k;
            double maxAbs = std::abs(a[k * n + k]);

            for (int row = k + 1; row < n; ++row)
            {
                const double v = std::abs(a[row * n + k]);
                if (v > maxAbs)
                {
                    maxAbs = v;
                    pivotRow = row;
                }
            }

            rowSwaps[static_cast<size_t>(k)] = pivotRow;

            if (pivotRow != k)
                std::swap_ranges(a + k * n, a + k * n + n, a + pivotRow * n);

            const double pivot = a[k * n + k];

            // A well-formed netlist can't produce a singular matrix (the companion
            // models give every reactive branch a finite conductance, and gmin
            // backstops the nonlinear ones), so this means something is wrong with
            // the netlist rather than with the numerics.
            if (std::abs(pivot) < 1.0e-300)
                return false;

            // Kept for the back substitution, which would otherwise divide by
            // this same pivot on every solve. The factorisation needs the
            // reciprocal anyway, so storing it makes the divides in solveInPlace
            // free rather than merely cheaper.
            const double invPivot = 1.0 / pivot;
            invDiagonal[static_cast<size_t>(k)] = invPivot;

            for (int row = k + 1; row < n; ++row)
            {
                const double factor = a[row * n + k] * invPivot;
                a[row * n + k] = factor; // stored as the L factor

                if (factor != 0.0)
                {
                    for (int col = k + 1; col < n; ++col)
                        a[row * n + col] -= factor * a[k * n + col];
                }
            }
        }

        return true;
    }

    void DenseSolver::solveInPlace(double* rhs) const noexcept
    {
        const int n = dimension;
        const double* a = matrix.data();

        // Apply the same row swaps the factorisation made.
        for (int k = 0; k < n; ++k)
        {
            const int pivotRow = rowSwaps[static_cast<size_t>(k)];
            if (pivotRow != k)
                std::swap(rhs[k], rhs[pivotRow]);
        }

        // Forward substitution (L has an implicit unit diagonal).
        for (int row = 1; row < n; ++row)
        {
            double sum = rhs[row];
            for (int col = 0; col < row; ++col)
                sum -= a[row * n + col] * rhs[col];
            rhs[row] = sum;
        }

        // Back substitution. Multiplying by the stored reciprocal rather than
        // dividing by the diagonal costs up to an ulp against a correctly
        // rounded divide -- immaterial next to a solver whose convergence
        // tolerance is 1e-4 and whose result is rounded to float -- and saves n
        // divides on every solve, which for a linear circuit is every sample.
        for (int row = n - 1; row >= 0; --row)
        {
            double sum = rhs[row];
            for (int col = row + 1; col < n; ++col)
                sum -= a[row * n + col] * rhs[col];
            rhs[row] = sum * invDiagonal[static_cast<size_t>(row)];
        }
    }
} // namespace CelineEngine
