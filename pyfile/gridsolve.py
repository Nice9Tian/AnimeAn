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


def label_regions(mask):
    """4-connected component labeling of a 2D boolean array ``mask``.

    Returns ``(labels, count)`` where ``labels`` is an ``int32`` ndarray of
    the same shape as ``mask`` (``0`` where ``mask`` is False, and
    ``1 .. count`` for each 4-connected True component) and ``count`` is the
    number of connected components.
    """
    np = _np()
    m = np.asarray(mask, dtype=bool)
    if m.ndim != 2:
        raise ValueError("mask must be a 2D boolean array")
    if m.size == 0:
        return np.zeros(m.shape, dtype=np.int32), 0
    is_start = m.copy()
    if m.shape[1] > 1:
        is_start[:, 1:] &= ~m[:, :-1]
    run_ids = np.cumsum(is_start, dtype=np.int32).reshape(m.shape)
    n_runs = int(run_ids[-1, -1])
    if n_runs == 0:
        return np.zeros(m.shape, dtype=np.int32), 0
    parent = list(range(n_runs + 1))
    if m.shape[0] > 1:
        v_adj = m[:-1, :] & m[1:, :]
        v_start = v_adj.copy()
        if m.shape[1] > 1:
            v_start[:, 1:] &= ~v_adj[:, :-1]
        u_arr = run_ids[:-1, :][v_start]
        v_arr = run_ids[1:, :][v_start]
        for u, v in zip(u_arr.tolist(), v_arr.tolist()):
            while parent[u] != u:
                parent[u] = parent[parent[u]]
                u = parent[u]
            while parent[v] != v:
                parent[v] = parent[parent[v]]
                v = parent[v]
            if u != v:
                if u < v:
                    parent[v] = u
                else:
                    parent[u] = v
    lut = [0] * (n_runs + 1)
    count = 0
    for i in range(1, n_runs + 1):
        p = parent[i]
        if p == i:
            count += 1
            lut[i] = count
        else:
            lut[i] = lut[p]
    lut_arr = np.asarray(lut, dtype=np.int32)
    labels = np.where(m, lut_arr[run_ids], np.int32(0))
    return labels, count
