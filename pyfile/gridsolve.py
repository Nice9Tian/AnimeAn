"""Generic regular-grid numerical helpers: preconditioned conjugate gradients
(PCG) and per-node 2D Jacobian determinants from one-sided stencils.
"""

import pydeps


def _np():
    np = pydeps.ensure("numpy")
    if np is None:
        import numpy as np
    return np


def pcg(apply_a, b, x0=None, precond=None, tol=1e-10, max_iter=1500,
        step_tol=None):
    """Solve ``A x = b`` by (preconditioned) conjugate gradients.

    Parameters
    ----------
    apply_a : callable
        Symmetric positive-definite linear operator mapping an array ``v``
        to ``A(v)`` of the same shape.
    b : ndarray
        Right-hand side array.
    x0 : ndarray, optional
        Initial guess. When ``None``, starts from zero with the exact
        floating-point operation order of an unseeded CG loop.
    precond : ndarray or callable, optional
        Diagonal preconditioner array ``M`` (applied as ``r / M``) or a
        callable ``M_inv(r)``.
    tol : float, optional
        Relative preconditioned residual-norm-squared stopping threshold
        ``(r, z) <= tol * (r0, z0)``.
    max_iter : int, optional
        Maximum number of iterations.
    step_tol : float, optional
        Optional L-infinity update stopping threshold: stop when the
        largest single-step coordinate change ``max(|alpha * p|) <= step_tol``.

    Returns
    -------
    (x, iterations) : (ndarray, int)
    """
    np = _np()
    if x0 is None:
        E = np.zeros_like(b)
        R = b.copy()
    else:
        E = np.array(x0, dtype=float, copy=True)
        R = b - apply_a(E)
    if precond is None:
        Z = R
    elif callable(precond):
        Z = precond(R)
    else:
        Z = R / precond
    P = Z.copy()
    rs = float(np.sum(R * Z))
    if rs <= 0.0:
        return E, 0
    rs0 = rs
    for it in range(1, max_iter + 1):
        AP = apply_a(P)
        denom = float(np.sum(P * AP)) or 1e-30
        alpha = rs / denom
        E += alpha * P
        if step_tol is not None and abs(alpha) * float(np.max(np.abs(P))) <= step_tol:
            return E, it
        R -= alpha * AP
        if precond is None:
            Z = R
        elif callable(precond):
            Z = precond(R)
        else:
            Z = R / precond
        rs_new = float(np.sum(R * Z))
        if tol is not None and rs_new <= tol * rs0:
            return E, it
        P = Z + (rs_new / rs) * P
        rs = rs_new
    return E, max_iter


def det2(dx, dy, du, dv):
    """Per-node 2D Jacobian determinant on a regular grid with cell steps
    ``(du, dv)``, taking the minimum over all four one-sided finite-difference
    stencils (left/right x down/up).
    """
    np = _np()
    sxx = (dx[1:, :] - dx[:-1, :]) / du
    sxy = (dx[:, 1:] - dx[:, :-1]) / dv
    syx = (dy[1:, :] - dy[:-1, :]) / du
    syy = (dy[:, 1:] - dy[:, :-1]) / dv

    def sided(edge, axis):
        if axis == 0:
            return (np.concatenate([edge[:1, :], edge], axis=0),
                    np.concatenate([edge, edge[-1:, :]], axis=0))
        return (np.concatenate([edge[:, :1], edge], axis=1),
                np.concatenate([edge, edge[:, -1:]], axis=1))

    xx_l, xx_r = sided(sxx, 0)
    yx_l, yx_r = sided(syx, 0)
    xy_d, xy_u = sided(sxy, 1)
    yy_d, yy_u = sided(syy, 1)
    best = None
    for xx, yx in ((xx_l, yx_l), (xx_r, yx_r)):
        for xy, yy in ((xy_d, yy_d), (xy_u, yy_u)):
            det = xx * yy - xy * yx
            best = det if best is None else np.minimum(best, det)
    return best
