"""Unit tests for pyfile/gridsolve.py on pure 2D Poisson problems and 2D grid
Jacobian determinants (without importing auto_mapping).
"""
import math
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if os.path.join(ROOT, "pyfile") not in sys.path:
    sys.path.insert(0, os.path.join(ROOT, "pyfile"))

import numpy as np
import gridsolve

assert "auto_mapping" not in sys.modules

# ---------------------------------------------------------------------------
# 1) 2D Poisson with known analytic / discrete eigenfunction solution
# ---------------------------------------------------------------------------
nx, ny = 40, 32
lx, ly = 2.0, 1.6
du, dv = lx / nx, ly / ny
gx = np.linspace(0.0, lx, nx + 1)
gy = np.linspace(0.0, ly, ny + 1)
X, Y = np.meshgrid(gx, gy, indexing="ij")

# Multi-mode smooth solution vanishing on the boundary [0, lx] x [0, ly]
u_exact = (
    X * (lx - X) * Y * (ly - Y)
    + 0.35 * np.sin(2.0 * math.pi * X / lx) * np.sin(3.0 * math.pi * Y / ly)
)
u_exact[0, :] = u_exact[-1, :] = 0.0
u_exact[:, 0] = u_exact[:, -1] = 0.0


def apply_lap5(Z):
    """5-point negative Laplacian -lap(Z) with homogeneous Dirichlet boundary."""
    fx = (Z[1:, :] - Z[:-1, :]) / du
    fy = (Z[:, 1:] - Z[:, :-1]) / dv
    out = np.zeros_like(Z)
    out[1:-1, :] -= (fx[1:, :] - fx[:-1, :]) / du
    out[:, 1:-1] -= (fy[:, 1:] - fy[:, :-1]) / dv
    out[0, :] = out[-1, :] = 0.0
    out[:, 0] = out[:, -1] = 0.0
    return out


rhs = apply_lap5(u_exact)
u_cold, it_cold = gridsolve.pcg(apply_lap5, rhs, tol=1e-12, max_iter=500)
err_cold = float(np.max(np.abs(u_cold - u_exact)))
assert err_cold < 1e-6, f"Known-solution 2D Poisson error too large: {err_cold}"
assert 1 <= it_cold < 500, it_cold
print(f"1) 2D Poisson known solution: converged in {it_cold} iters, max err {err_cold:.2e}")

# Zero RHS returns immediately in 0 iterations
u_zero, it_zero = gridsolve.pcg(apply_lap5, np.zeros_like(rhs))
assert it_zero == 0 and float(np.max(np.abs(u_zero))) == 0.0

# ---------------------------------------------------------------------------
# 2) Warm start (x0): starting near the solution with step_tol stops early
# ---------------------------------------------------------------------------
pert = 0.05 * np.exp(-((X - 1.0) ** 2 + (Y - 0.8) ** 2) / 0.04)
pert[0, :] = pert[-1, :] = 0.0
pert[:, 0] = pert[:, -1] = 0.0
x0_near = u_exact + pert
u_warm, it_warm = gridsolve.pcg(apply_lap5, rhs, x0=x0_near, tol=1e-8, max_iter=500)
err_warm = float(np.max(np.abs(u_warm - u_exact)))
assert err_warm < 1e-5, err_warm
assert it_warm < it_cold, (it_warm, it_cold)

# With step_tol, starting from a nearby iterate terminates in a handful of steps
u_step, it_step = gridsolve.pcg(
    apply_lap5, rhs, x0=x0_near, tol=1e-12, max_iter=500, step_tol=1e-4
)
assert it_step < it_warm < it_cold, (it_step, it_warm, it_cold)
assert float(np.max(np.abs(u_step - u_exact))) < 2e-3
print(f"2) with initial guess x0: warm tol in {it_warm} iters, step_tol in {it_step} iters (cold {it_cold})")

# ---------------------------------------------------------------------------
# 3) Anisotropic / variable-coefficient Poisson with Jacobi preconditioner
# ---------------------------------------------------------------------------
# Spatially varying stiffness weight (1 inside, 50 in a central patch)
wx = np.ones((nx, ny + 1))
wy = np.ones((nx + 1, ny))
wx[nx // 4: 3 * nx // 4, ny // 4: 3 * ny // 4] = 50.0
wy[nx // 4: 3 * nx // 4, ny // 4: 3 * ny // 4] = 50.0
kappa = np.zeros_like(X)
kappa[nx // 3: 2 * nx // 3, ny // 3: 2 * ny // 3] = 200.0


def apply_var_a(Z):
    fx = wx * (Z[1:, :] - Z[:-1, :]) / du
    fy = wy * (Z[:, 1:] - Z[:, :-1]) / dv
    out = kappa * Z
    out[1:-1, :] -= (fx[1:, :] - fx[:-1, :]) / du
    out[:, 1:-1] -= (fy[:, 1:] - fy[:, :-1]) / dv
    out[0, :] = out[-1, :] = 0.0
    out[:, 0] = out[:, -1] = 0.0
    return out


diag = np.zeros_like(X)
diag[1:-1, :] += (wx[1:, :] + wx[:-1, :]) / (du * du)
diag[:, 1:-1] += (wy[:, 1:] + wy[:, :-1]) / (dv * dv)
diag += kappa
diag = np.maximum(diag, 1e-30)

b_var = apply_var_a(u_exact)
u_unprec, it_unprec = gridsolve.pcg(apply_var_a, b_var, tol=1e-10, max_iter=1000)
u_prec_arr, it_prec_arr = gridsolve.pcg(
    apply_var_a, b_var, precond=diag, tol=1e-10, max_iter=1000
)
u_prec_fn, it_prec_fn = gridsolve.pcg(
    apply_var_a, b_var, precond=lambda r: r / diag, tol=1e-10, max_iter=1000
)
assert float(np.max(np.abs(u_prec_arr - u_exact))) < 1e-4
assert np.array_equal(u_prec_arr, u_prec_fn) and it_prec_arr == it_prec_fn
assert it_prec_arr < it_unprec, (it_prec_arr, it_unprec)
print(f"3) preconditioned CG: {it_prec_arr} iters vs unpreconditioned {it_unprec} iters")

# ---------------------------------------------------------------------------
# 4) max_iter truncation
# ---------------------------------------------------------------------------
cap = 5
u_trunc, it_trunc = gridsolve.pcg(apply_var_a, b_var, tol=1e-30, max_iter=cap)
assert it_trunc == cap, (it_trunc, cap)
# Residual after `cap` iterations is strictly smaller than the initial residual
r0_norm = float(np.linalg.norm(b_var))
r_trunc_norm = float(np.linalg.norm(b_var - apply_var_a(u_trunc)))
assert r_trunc_norm < r0_norm, (r_trunc_norm, r0_norm)
print(f"4) max_iter={cap} truncation: stopped at {it_trunc} iters, residual {r0_norm:.1f} -> {r_trunc_norm:.1f}")

# ---------------------------------------------------------------------------
# 5) det2: affine map and single-row fold detection
# ---------------------------------------------------------------------------
# Affine map (x, y) -> (2x + 0.5y, -0.3x + 1.5y) has constant det = 2*1.5 - 0.5*(-0.3) = 3.15
dx_aff = 2.0 * X + 0.5 * Y
dy_aff = -0.3 * X + 1.5 * Y
d_aff = gridsolve.det2(dx_aff, dy_aff, du, dv)
assert d_aff.shape == X.shape
assert float(np.max(np.abs(d_aff - 3.15))) < 1e-12

# Single-column fold: dx reverses across column i=10..11 while neighbouring columns advance
dx_fold = X.copy()
dx_fold[11:, :] = dx_fold[11:, :] - 2.5 * du
dy_fold = Y.copy()
d_fold = gridsolve.det2(dx_fold, dy_fold, du, dv)
assert float(np.min(d_fold)) < -1.0, float(np.min(d_fold))
print("5) det2: exact on affine map (3.15) and detects single-cell fold")

assert "auto_mapping" not in sys.modules
print("t_gridsolve: ALL OK")
