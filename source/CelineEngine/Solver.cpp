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
        transposed.assign(static_cast<size_t>(n * n), 0.0);
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

        // The substitutions below walk the factors a column at a time, so they
        // want each column contiguous. Copied once here rather than built up
        // during the elimination, because a later pivot swaps whole rows --
        // the L entries already written included -- and only the finished
        // factorisation says where everything ended up. O(n^2) against the
        // elimination's O(n^3).
        double* t = transposed.data();
        for (int row = 0; row < n; ++row)
            for (int col = 0; col < n; ++col)
                t[col * n + row] = a[row * n + col];

        return true;
    }

    bool DenseSolver::factoriseAndSolve(double* rhs) noexcept
    {
        const int n = dimension;
        double* a = matrix.data();

        // The same elimination as factorise(), pivot for pivot, with the
        // right-hand side swapped and reduced alongside each row.
        for (int k = 0; k < n; ++k)
        {
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

            if (pivotRow != k)
            {
                std::swap_ranges(a + k * n + k, a + k * n + n, a + pivotRow * n + k);
                std::swap(rhs[k], rhs[pivotRow]);
            }

            const double pivot = a[k * n + k];
            if (std::abs(pivot) < 1.0e-300)
                return false;

            const double invPivot = 1.0 / pivot;
            invDiagonal[static_cast<size_t>(k)] = invPivot;

            for (int row = k + 1; row < n; ++row)
            {
                const double factor = a[row * n + k] * invPivot;
                if (factor != 0.0)
                {
                    for (int col = k + 1; col < n; ++col)
                        a[row * n + col] -= factor * a[k * n + col];

                    rhs[row] -= factor * rhs[k];
                }
            }
        }

        // Back substitution a column at a time, as in solveInPlace(), though
        // here the column is strided: copying U out to make it contiguous would
        // cost as much as the substitution it speeds up. Each row is still
        // independent of the others, which is what matters.
        for (int col = n - 1; col >= 0; --col)
        {
            const double x = rhs[col] * invDiagonal[static_cast<size_t>(col)];
            rhs[col] = x;
            if (x == 0.0)
                continue;

            for (int row = 0; row < col; ++row)
                rhs[row] -= a[row * n + col] * x;
        }

        return true;
    }

    void DenseSolver::solveInPlace(double* rhs) const noexcept
    {
        const int n = dimension;
        const double* t = transposed.data();

        // Apply the same row swaps the factorisation made.
        for (int k = 0; k < n; ++k)
        {
            const int pivotRow = rowSwaps[static_cast<size_t>(k)];
            if (pivotRow != k)
                std::swap(rhs[k], rhs[pivotRow]);
        }

        // Both substitutions go a column at a time: once an unknown is known,
        // its contribution is taken off every row still waiting on it. Written
        // a row at a time instead -- a dot product per row -- every
        // multiply-add waits on the one before it, and on a 19-port sheet that
        // chain made this the single most expensive function in the engine.
        // Column by column, the rows are independent and the inner loop
        // vectorises, which is what the transposed copy is for.
        //
        // The forward pass subtracts in the same order as the row form did, so
        // it is bit-identical to it. The back pass cannot be: its unknowns
        // emerge last-first, so each row's terms are now taken off in the
        // opposite order -- a difference in the last bits of a double, far
        // below the Newton tolerance this feeds.

        // Forward substitution (L has an implicit unit diagonal).
        for (int col = 0; col < n - 1; ++col)
        {
            const double x = rhs[col];
            if (x == 0.0)
                continue;

            const double* column = t + col * n;
            for (int row = col + 1; row < n; ++row)
                rhs[row] -= column[row] * x;
        }

        // Back substitution. Multiplying by the stored reciprocal rather than
        // dividing by the diagonal costs up to an ulp against a correctly
        // rounded divide -- immaterial next to a solver whose convergence
        // tolerance is 1e-4 and whose result is rounded to float -- and saves n
        // divides on every solve, which for a linear circuit is every sample.
        for (int col = n - 1; col >= 0; --col)
        {
            const double x = rhs[col] * invDiagonal[static_cast<size_t>(col)];
            rhs[col] = x;
            if (x == 0.0)
                continue;

            const double* column = t + col * n;
            for (int row = 0; row < col; ++row)
                rhs[row] -= column[row] * x;
        }
    }
} // namespace CelineEngine
