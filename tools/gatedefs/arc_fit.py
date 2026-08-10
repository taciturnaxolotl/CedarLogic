#!/usr/bin/env python3
"""Convert baked chord runs in cl_gatedefs.xml gate shapes into <arc> primitives.

Gate outlines store curves as ~20 straight <line> chords each. This finds maximal
runs of consecutive, connected chords whose vertices lie on a common circle and
rewrites each run as a single <arc>cx,cy,r,startDeg,sweepDeg</arc>. Straight
segments are left as <line>. Angles follow the renderer's convention: degrees
from +Y (up) increasing clockwise toward +X, i.e. a point at angle t is
(cx + r*sin t, cy + r*cos t).

Only <line> elements directly under <shape> are considered; <offset>/<label_offset>
wrapped geometry is left untouched (its coords are relative). The transform is
line-based so all other formatting is preserved byte-for-byte.

Usage:
  python arc_fit.py res/cl_gatedefs.xml            # report only
  python arc_fit.py res/cl_gatedefs.xml --write    # rewrite in place
"""
import sys, re, math

LINE_RE = re.compile(r'^(\s*)<line>([-0-9.eE]+),([-0-9.eE]+),([-0-9.eE]+),([-0-9.eE]+)</line>\s*$')
SHAPE_OPEN = re.compile(r'<shape>')
SHAPE_CLOSE = re.compile(r'</shape>')
OFFSET_OPEN = re.compile(r'<(offset|label_offset)>')
GATE_NAME = re.compile(r'<gate>\s*<name>([^<]+)</name>')

EPS_CONNECT = 0.02   # endpoints closer than this are "connected"
MAX_RESID = 0.03     # max distance of a vertex from the fitted circle (gate units)
MIN_CHORDS = 4       # a run shorter than this stays as lines
MAX_RADIUS = 60.0    # above this the run is effectively straight -> keep as lines


def fit_circle(pts):
    """Algebraic (Kasa) circle fit. Returns (cx, cy, r) or None if degenerate."""
    n = len(pts)
    sx = sy = sxx = syy = sxy = sxz = syz = sz = 0.0
    for (x, y) in pts:
        z = x * x + y * y
        sx += x; sy += y; sxx += x * x; syy += y * y; sxy += x * y
        sxz += x * z; syz += y * z; sz += z
    # Solve the normal equations for center (a, b) and c:  [ [sxx sxy sx][sxy syy sy][sx sy n] ] [a b c]^T = [sxz syz sz]/...
    A = [[sxx, sxy, sx], [sxy, syy, sy], [sx, sy, n]]
    B = [sxz, syz, sz]
    det = (A[0][0]*(A[1][1]*A[2][2]-A[1][2]*A[2][1])
           - A[0][1]*(A[1][0]*A[2][2]-A[1][2]*A[2][0])
           + A[0][2]*(A[1][0]*A[2][1]-A[1][1]*A[2][0]))
    if abs(det) < 1e-12:
        return None
    def solve_col(col):
        M = [row[:] for row in A]
        for i in range(3):
            M[i][col] = B[i]
        return (M[0][0]*(M[1][1]*M[2][2]-M[1][2]*M[2][1])
                - M[0][1]*(M[1][0]*M[2][2]-M[1][2]*M[2][0])
                + M[0][2]*(M[1][0]*M[2][1]-M[1][1]*M[2][0])) / det
    a = solve_col(0); b = solve_col(1); c = solve_col(2)
    cx = a / 2.0; cy = b / 2.0
    r2 = c + cx * cx + cy * cy
    if r2 <= 0:
        return None
    return (cx, cy, math.sqrt(r2))


def max_residual(pts, cx, cy, r):
    return max(abs(math.hypot(x - cx, y - cy) - r) for (x, y) in pts)


def angle_of(x, y, cx, cy):
    # Compass convention: t such that x=cx+r*sin t, y=cy+r*cos t  ->  t=atan2(dx,dy)
    return math.degrees(math.atan2(x - cx, y - cy))


def norm180(d):
    while d > 180: d -= 360
    while d < -180: d += 360
    return d


def fmt(v):
    # Match the file's style: trim to a short decimal, drop trailing zeros.
    s = f"{v:.4f}".rstrip('0').rstrip('.')
    return s if s not in ('-0', '') else '0'


def try_arc(run):
    """run: list of (x1,y1,x2,y2). Return an <arc> payload string or None."""
    if len(run) < MIN_CHORDS:
        return None
    verts = [(run[0][0], run[0][1])] + [(s[2], s[3]) for s in run]
    fit = fit_circle(verts)
    if not fit:
        return None
    cx, cy, r = fit
    if r > MAX_RADIUS or max_residual(verts, cx, cy, r) > MAX_RESID:
        return None
    # Vertices alone aren't enough: a rectangle's or triangle's corners are also
    # concyclic. Require each chord's MIDPOINT to sit on the circle too -- a true
    # arc's chord barely sags, but a box/triangle side's midpoint is far off.
    mids = [((s[0] + s[2]) * 0.5, (s[1] + s[3]) * 0.5) for s in run]
    if max_residual(mids, cx, cy, r) > MAX_RESID:
        return None
    # Signed sweep: sum of normalized deltas so wrap-around and direction are right.
    angs = [angle_of(x, y, cx, cy) for (x, y) in verts]
    sweep = 0.0
    for i in range(1, len(angs)):
        step = norm180(angs[i] - angs[i - 1])
        if abs(step) < 1e-6:  # duplicate point
            continue
        sweep += step
    start = angs[0]
    return f"{fmt(cx)},{fmt(cy)},{fmt(r)},{fmt(start)},{fmt(sweep)}"


def process_shape(body_lines):
    """body_lines: raw file lines strictly between <shape> and </shape>.
    Returns (new_lines, n_runs, n_lines_removed)."""
    # Parse into tokens: ('line', indent, (x1,y1,x2,y2), raw) or ('other', raw).
    toks = []
    for raw in body_lines:
        m = LINE_RE.match(raw)
        if m and not any(OFFSET_OPEN.search(x) for x in []):  # top-level line
            indent = m.group(1)
            coords = tuple(float(m.group(i)) for i in range(2, 6))
            toks.append(('line', indent, coords, raw))
        else:
            toks.append(('other', None, None, raw))

    out = []
    n_runs = 0
    n_removed = 0
    i = 0
    inside_offset = False
    while i < len(toks):
        t = toks[i]
        if t[0] == 'other':
            if OFFSET_OPEN.search(t[3]):
                inside_offset = True
            if re.search(r'</(offset|label_offset)>', t[3]):
                inside_offset = False
            out.append(t[3])
            i += 1
            continue
        # A run of connected top-level lines (only outside offset wrappers).
        if inside_offset:
            out.append(t[3]); i += 1; continue
        run = [t[2]]
        indent = t[1]
        j = i + 1
        while j < len(toks) and toks[j][0] == 'line':
            prev = run[-1]
            cur = toks[j][2]
            if abs(prev[2] - cur[0]) <= EPS_CONNECT and abs(prev[3] - cur[1]) <= EPS_CONNECT:
                run.append(cur)
                j += 1
            else:
                break
        # Greedy: find the LONGEST prefix of the run that fits one circle.
        payload = None
        best_k = 0
        if len(run) >= MIN_CHORDS:
            for k in range(len(run), MIN_CHORDS - 1, -1):
                p = try_arc(run[:k])
                if p:
                    payload = p; best_k = k; break
        if payload:
            out.append(f"{indent}<arc>{payload}</arc>")
            n_runs += 1
            n_removed += best_k
            i += best_k
        else:
            out.append(t[3])
            i += 1
    return out, n_runs, n_removed


def main():
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(1)
    path = sys.argv[1]
    write = '--write' in sys.argv[2:]
    with open(path, 'r', encoding='utf-8', newline='') as f:
        lines = f.read().split('\n')

    out = []
    i = 0
    gate = '?'
    total_runs = total_removed = gates_touched = 0
    while i < len(lines):
        gm = GATE_NAME.search(lines[i])
        if gm:
            gate = gm.group(1)
        if SHAPE_OPEN.search(lines[i]):
            # Collect the shape body up to </shape>.
            out.append(lines[i])
            j = i + 1
            body = []
            while j < len(lines) and not SHAPE_CLOSE.search(lines[j]):
                body.append(lines[j]); j += 1
            new_body, n_runs, n_removed = process_shape(body)
            out.extend(new_body)
            if j < len(lines):
                out.append(lines[j])  # the </shape> line
            if n_runs:
                print(f"  {gate:<28} {n_runs} arc(s), {n_removed} chords -> arcs")
                total_runs += n_runs; total_removed += n_removed; gates_touched += 1
            i = j + 1
        else:
            out.append(lines[i]); i += 1

    print(f"\nTotal: {gates_touched} gates, {total_runs} arcs, {total_removed} chords replaced")
    if write:
        with open(path, 'w', encoding='utf-8', newline='') as f:
            f.write('\n'.join(out))
        print(f"Wrote {path}")
    else:
        print("(dry run; pass --write to apply)")


if __name__ == '__main__':
    main()
