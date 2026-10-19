#!/usr/bin/env python3
"""Validate .drawio files: XML well-formedness, vertex overlap, edge-vs-box collision."""
import sys, xml.etree.ElementTree as ET

def parse(path):
    tree = ET.parse(path)
    root = tree.getroot()
    cells = {}
    for c in root.iter('mxCell'):
        cid = c.get('id')
        style = c.get('style') or ''
        geom = c.find('mxGeometry')
        g = None
        if geom is not None:
            g = {'x': float(geom.get('x', 0)), 'y': float(geom.get('y', 0)),
                 'w': float(geom.get('width', 0)), 'h': float(geom.get('height', 0))}
        pts = []
        if geom is not None:
            arr = geom.find('Array')
            if arr is not None:
                for p in arr.findall('mxPoint'):
                    pts.append((float(p.get('x')), float(p.get('y'))))
        cells[cid] = {'id': cid, 'parent': c.get('parent'), 'vertex': c.get('vertex') == '1',
                      'edge': c.get('edge') == '1', 'style': style, 'geom': g, 'pts': pts,
                      'source': c.get('source'), 'target': c.get('target'),
                      'value': (c.get('value') or '')[:30]}
    return cells

def abs_geom(cells, cid, cache):
    if cid in cache:
        return cache[cid]
    c = cells[cid]
    if c['geom'] is None:
        cache[cid] = None
        return None
    if cid in ('0', '1') or not c['parent'] or c['parent'] not in cells:
        cache[cid] = dict(c['geom'])
        return cache[cid]
    pg = abs_geom(cells, c['parent'], cache)
    if pg is None:
        cache[cid] = dict(c['geom'])
    else:
        cache[cid] = {'x': c['geom']['x'] + pg['x'], 'y': c['geom']['y'] + pg['y'],
                      'w': c['geom']['w'], 'h': c['geom']['h']}
    return cache[cid]

def pin(edge, cells, cid, which, cache):
    """Port point on vertex cid, using pins declared in the EDGE's style."""
    g = abs_geom(cells, cid, cache)
    if g is None:
        return None
    px, py = 0.5, 0.5
    for tok in edge['style'].split(';'):
        tok = tok.strip()
        if tok.startswith(which + 'X='):
            px = float(tok.split('=')[1])
        if tok.startswith(which + 'Y='):
            py = float(tok.split('=')[1])
    return (g['x'] + px * g['w'], g['y'] + py * g['h'])

def seg_rect_intersect(p, q, r, pad=2.0):
    """Does segment p-q intersect rect r (inflated by pad)?"""
    x0, y0 = r['x'] - pad, r['y'] - pad
    x1, y1 = r['x'] + r['w'] + pad, r['y'] + r['h'] + pad
    # Liang-Barsky
    dx, dy = q[0] - p[0], q[1] - p[1]
    t0, t1 = 0.0, 1.0
    for pp, qq in ((-dx, p[0] - x0), (dx, x1 - p[0]), (-dy, p[1] - y0), (dy, y1 - p[1])):
        if abs(pp) < 1e-9:
            if qq < 0:
                return False
        else:
            t = qq / pp
            if pp < 0:
                if t > t1: return False
                if t > t0: t0 = t
            else:
                if t < t0: return False
                if t < t1: t1 = t
    return True

def check(path):
    cells = parse(path)
    cache = {}
    problems = []
    verts = [c for c in cells.values() if c['vertex'] and c['geom'] is not None]
    # 1. vertex-vertex overlap (skip ancestor/descendant pairs)
    def ancestors(cid):
        out, cur = set(), cid
        while cur in cells and cells[cur]['parent'] in cells:
            cur = cells[cur]['parent']
            out.add(cur)
            if len(out) > 50: break
        return out
    anc = {c['id']: ancestors(c['id']) for c in verts}
    for i in range(len(verts)):
        for j in range(i + 1, len(verts)):
            a, b = verts[i], verts[j]
            if b['id'] in anc[a['id']] or a['id'] in anc[b['id']]:
                continue
            ga, gb = abs_geom(cells, a['id'], cache), abs_geom(cells, b['id'], cache)
            if not ga or not gb:
                continue
            ox = min(ga['x'] + ga['w'], gb['x'] + gb['w']) - max(ga['x'], gb['x'])
            oy = min(ga['y'] + ga['h'], gb['y'] + gb['h']) - max(ga['y'], gb['y'])
            if ox > 1 and oy > 1:
                problems.append(f"BOX-OVERLAP: {a['id']}({a['value']}) x {b['id']}({b['value']}) ox={ox:.0f} oy={oy:.0f}")
    # 2. edge vs box collision
    for c in cells.values():
        if not c['edge'] or not c['source'] or not c['target']:
            continue
        s = pin(c, cells, c['source'], 'exit', cache)
        t = pin(c, cells, c['target'], 'entry', cache)
        if s is None or t is None:
            continue
        pts = [s] + c['pts'] + [t]
        if len(c['pts']) == 0:
            # approximate router: same-x -> vertical, same-y -> horizontal, else L via corner
            if abs(s[0] - t[0]) < 3 or abs(s[1] - t[1]) < 3:
                pass
            else:
                mid = (t[0], s[1])
                pts = [s, mid, t]
        skip = {c['source'], c['target']}
        skip |= anc.get(c['source'], set()) | anc.get(c['target'], set())
        for v in verts:
            if v['id'] in skip:
                continue
            gv = abs_geom(cells, v['id'], cache)
            if not gv:
                continue
            for k in range(len(pts) - 1):
                if seg_rect_intersect(pts[k], pts[k + 1], gv):
                    problems.append(f"EDGE-CROSS: edge {c['id']} seg {pts[k]}->{pts[k+1]} crosses box {v['id']}({v['value']})")
                    break
    # 3. edge-edge overlap (collinear shared segments)
    edges = []
    for c in cells.values():
        if not c['edge'] or not c['source'] or not c['target']:
            continue
        s = pin(c, cells, c['source'], 'exit', cache)
        t = pin(c, cells, c['target'], 'entry', cache)
        if s is None or t is None:
            continue
        pts = [s] + c['pts'] + [t]
        if len(c['pts']) == 0 and abs(s[0] - t[0]) > 3 and abs(s[1] - t[1]) > 3:
            pts = [s, (t[0], s[1]), t]
        edges.append((c['id'], pts))
    for i in range(len(edges)):
        for j in range(i + 1, len(edges)):
            id1, p1 = edges[i]
            id2, p2 = edges[j]
            for a in range(len(p1) - 1):
                for b in range(len(p2) - 1):
                    s1, e1 = p1[a], p1[a + 1]
                    s2, e2 = p2[b], p2[b + 1]
                    # axis-aligned collinear overlap check
                    def rng(v): return (min(v[0], e_[0]), max(v[0], e_[0]), min(v[1], e_[1]), max(v[1], e_[1]))
                    for (A, B, C, D) in ((s1, e1, s2, e2),):
                        if abs(A[1] - B[1]) < 1 and abs(C[1] - D[1]) < 1 and abs(A[1] - C[1]) < 1:
                            lo = max(min(A[0], B[0]), min(C[0], D[0]))
                            hi = min(max(A[0], B[0]), max(C[0], D[0]))
                            if hi - lo > 4:
                                problems.append(f"EDGE-OVERLAP(h): {id1} x {id2} at y={A[1]:.0f} x[{lo:.0f},{hi:.0f}]")
                        if abs(A[0] - B[0]) < 1 and abs(C[0] - D[0]) < 1 and abs(A[0] - C[0]) < 1:
                            lo = max(min(A[1], B[1]), min(C[1], D[1]))
                            hi = min(max(A[1], B[1]), max(C[1], D[1]))
                            if hi - lo > 4:
                                problems.append(f"EDGE-OVERLAP(v): {id1} x {id2} at x={A[0]:.0f} y[{lo:.0f},{hi:.0f}]")
    return problems

if __name__ == '__main__':
    total = 0
    for p in sys.argv[1:]:
        probs = check(p)
        print(f"== {p}: {'OK' if not probs else str(len(probs)) + ' problems'}")
        for x in probs:
            print("   ", x)
        total += len(probs)
    sys.exit(1 if total else 0)
