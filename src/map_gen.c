#include "map_gen.h"

#include <raylib.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "utils.c"
#include "heightmap_tools.c"

/* Height colour ramp used by gen_voronoi_image(). */
static const GradientStop heightmap_levels[] = {
  { 0.00f,  {90, 75, 150, 255} },
  { 0.19f,  {69, 146, 163, 255} },
  { 0.20f,  {105, 189, 169, 255} },
  { 0.50f,  {221, 247, 114, 255} },
  { 0.90f,  {219, 94, 66, 255} },
  { 1.01f,  {219, 48, 48, 255} }
};
static const int heightmap_n = sizeof(heightmap_levels) / sizeof(heightmap_levels[0]);

/* -------------------------------------------------------------------------
 * Internal hull edges. The recursion walks convex hull rings; the public Edge
 * record (map_gen.h) is the output and carries no links.
 * ---------------------------------------------------------------------- */

struct HullEdge {
  int org, dest; // Origin and destination site indices
  struct HullEdge *next, *prev; // Next/previous edge on the hull ring (CCW)
  struct HullEdge *opposite;    // Opposite half-edge (2-point case only)
  int idx;                      // Creation index (debug)
};

/* Divide-and-conquer Delaunay triangulation (Lee/Preparata - Guibas/Stolfi style).
 * Centers must be sorted by x (then y). triangulate() handles centers[start, end)
 * and returns the convex hull of that subrange as a CCW edge ring: *left_e is
 * the hull edge leaving the leftmost site, *right_e the one leaving the
 * rightmost. Finished triangles accumulate in map->tris. */

static int compare_sites(const void *a_v, const void *b_v)
{
	Center *a = (Center *) a_v;
	Center *b = (Center *) b_v;

	float diff = a->pos.x - b->pos.x;

	if (diff == 0) {
		diff = a->pos.y - b->pos.y;
	}

	return (diff>0) - (diff<0);
}

static double orient2d(Vector2 a, Vector2 b, Vector2 c)
{
	return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

static double dist2(Vector2 a, Vector2 b)
{
	double dx = a.x - b.x;
	double dy = a.y - b.y;
	return dx * dx + dy * dy;
}

/* > 0 iff d lies strictly inside the circumcircle of CCW triangle (a, b, c). */
static double incircle(Vector2 a, Vector2 b, Vector2 c, Vector2 d)
{
	double adx = a.x - d.x, ady = a.y - d.y;
	double bdx = b.x - d.x, bdy = b.y - d.y;
	double cdx = c.x - d.x, cdy = c.y - d.y;

	double abdet = adx * bdy - bdx * ady;
	double bcdet = bdx * cdy - cdx * bdy;
	double cadet = cdx * ady - adx * cdy;

	double a2 = adx * adx + ady * ady;
	double b2 = bdx * bdx + bdy * bdy;
	double c2 = cdx * cdx + cdy * cdy;

	return a2 * bcdet + b2 * cadet + c2 * abdet;
}

/* Forward declarations (defined with the post-pass / graph helpers below). */
static int point_in_tri(Vector2 p, Vector2 a, Vector2 b, Vector2 c);
static void edge_index_free(MapData *map);
static void edge_index_rebuild(MapData *map);

static HullEdge *new_hull(MapData *map, int org, int dest)
{
	if (map->n_howned == map->cap_howned) {
		map->cap_howned = map->cap_howned ? map->cap_howned * 2 : 64;
		map->howned = realloc(map->howned, sizeof(HullEdge *) * (size_t)map->cap_howned);
	}
	HullEdge *e = malloc(sizeof(HullEdge));
	e->org = org;
	e->dest = dest;
	e->next = e->prev = NULL;
	e->opposite = NULL;
	e->idx = map->n_howned;
	map->howned[map->n_howned++] = e;
	return e;
}

/* The hull rings are scratch space for one triangulation run. */
static void free_hull_edges(MapData *map)
{
	for (int i = 0; i < map->n_howned; i++) free(map->howned[i]);
	free(map->howned);
	map->howned = NULL;
	map->n_howned = 0;
	map->cap_howned = 0;
}

/* Store triangle (a, b, c); enforces CCW order, drops degenerate (collinear) ones. */
static void push_tri(MapData *map, int a, int b, int c)
{
	double o = orient2d(map->c[a].pos, map->c[b].pos, map->c[c].pos);
	if (fabs(o) < 1e-9) return;

	if (o < 0) {
		int t = b;
		b = c;
		c = t;
	}

	if (map->n_tris == map->cap_tris) {
		map->cap_tris = map->cap_tris ? map->cap_tris * 2 : 64;
		map->tris = realloc(map->tris, sizeof(int) * 3 * (size_t)map->cap_tris);
	}

	map->tris[3 * map->n_tris + 0] = a;
	map->tris[3 * map->n_tris + 1] = b;
	map->tris[3 * map->n_tris + 2] = c;
	map->n_tris++;
}

static int ring_count(HullEdge *start)
{
	int n = 0;
	HullEdge *e = start;
	if (!e) return 0;
	do {
		n++;
		e = e->next;
		if (n > 1000000) break; // Safety against a corrupted ring
	} while (e && e != start);
	return n;
}

static int read_ring(HullEdge *start, int *out, int cap)
{
	int n = 0;
	HullEdge *e = start;
	if (!e) return 0;
	do {
		if (n >= cap) break;
		out[n++] = e->org;
		e = e->next;
	} while (e && e != start);
	return n;
}

/* NOTE on collinear hull points: they are deliberately KEPT in the hull rings.
 * The seam bubble steps through them one at a time (emitting degenerate
 * triangles that are skipped), which subdivides the strip instead of emitting
 * giant spanning triangles. Tangent selection picks extreme endpoints among
 * same-slope ties, stranded middles are reinserted by insert_missing_sites(),
 * and T-junctions are split to keep the mesh conforming. */

/* Lower common tangent of the left hull (h1) and right hull (h2), directed h1 -> h2.
 * Correct selection: among all pairs with every site on the left, the tangent
 * minimizes the slope (steepest downward line that keeps everything above).
 * (Longest-valid is NOT the tangent: it can touch a hull off its lower chain.) */
static void lower_tangent(MapData *map, int *h1, int n1, int *h2, int n2, int *bi, int *bj)
{
	const double EPS = 1e-9;
	int found = 0;
	int li = 0, lj = 0;
	double bdy = 0, bdx = 1, bestd = -1;

	for (int i = 0; i < n1; i++) {
		for (int j = 0; j < n2; j++) {
			Vector2 a = map->c[h1[i]].pos;
			Vector2 b = map->c[h2[j]].pos;
			double dx = b.x - a.x;
			if (dx <= EPS) continue; // Strictly separated in x (sorted split)
			int ok = 1;
			for (int k = 0; k < n1 && ok; k++) {
				if (k != i && orient2d(a, b, map->c[h1[k]].pos) < -EPS) ok = 0;
			}
			for (int k = 0; k < n2 && ok; k++) {
				if (k != j && orient2d(a, b, map->c[h2[k]].pos) < -EPS) ok = 0;
			}
			if (!ok) continue;
			double dy = b.y - a.y;
			// Exact slope comparison, no division. Equal slopes (same line for
			// convex chains) tie-break to the longest pair = extreme endpoints.
			double lhs = dy * bdx, rhs = bdy * dx;
			if (!found || lhs < rhs || (lhs == rhs && dist2(a, b) > bestd)) {
				found = 1;
				bdy = dy;
				bdx = dx;
				bestd = dist2(a, b);
				li = i;
				lj = j;
			}
		}
	}

	if (!found) {
		// Degenerate (vertical/collinear): fall back to the longest valid pair.
		double best = -1;
		for (int i = 0; i < n1; i++) {
			for (int j = 0; j < n2; j++) {
				Vector2 a = map->c[h1[i]].pos;
				Vector2 b = map->c[h2[j]].pos;
				int ok = 1;
				for (int k = 0; k < n1 && ok; k++) {
					if (k != i && orient2d(a, b, map->c[h1[k]].pos) < -EPS) ok = 0;
				}
				for (int k = 0; k < n2 && ok; k++) {
					if (k != j && orient2d(a, b, map->c[h2[k]].pos) < -EPS) ok = 0;
				}
				if (ok) {
					double d = dist2(a, b);
					if (d > best) {
						best = d;
						li = i;
						lj = j;
					}
				}
			}
		}
	}

	*bi = li;
	*bj = lj;
}

/* Upper common tangent, directed h2 -> h1 (right-to-left along the top).
 * Mirror image: among all pairs with every site on the left, maximize slope. */
static void upper_tangent(MapData *map, int *h1, int n1, int *h2, int n2, int *bi, int *bj)
{
	const double EPS = 1e-9;
	int found = 0;
	int li = 0, lj = 0;
	double bdy = 0, bdx = 1, bestd = -1;

	for (int i = 0; i < n1; i++) {
		for (int j = 0; j < n2; j++) {
			Vector2 a = map->c[h2[j]].pos;
			Vector2 b = map->c[h1[i]].pos;
			double dx = b.x - a.x;
			if (dx >= -EPS) continue; // Must head strictly westward
			int ok = 1;
			for (int k = 0; k < n1 && ok; k++) {
				if (k != i && orient2d(a, b, map->c[h1[k]].pos) < -EPS) ok = 0;
			}
			for (int k = 0; k < n2 && ok; k++) {
				if (k != j && orient2d(a, b, map->c[h2[k]].pos) < -EPS) ok = 0;
			}
			if (!ok) continue;
			// Slope of the undirected line: (b.y - a.y) / (b.x - a.x), dx < 0.
			// Maximize it via cross products (ndx > 0, so max dy/dx = min dy/ndx).
			// Equal slopes tie-break to the longest pair = extreme endpoints.
			double dy = b.y - a.y;
			double ndx = -dx;
			double lhs = dy * bdx, rhs = bdy * ndx;
			if (!found || lhs < rhs || (lhs == rhs && dist2(a, b) > bestd)) {
				found = 1;
				bdy = dy;
				bdx = ndx;
				bestd = dist2(a, b);
				li = i;
				lj = j;
			}
		}
	}

	if (!found) {
		// Degenerate (vertical/collinear): fall back to the longest valid pair.
		double best = -1;
		for (int i = 0; i < n1; i++) {
			for (int j = 0; j < n2; j++) {
				Vector2 a = map->c[h2[j]].pos;
				Vector2 b = map->c[h1[i]].pos;
				int ok = 1;
				for (int k = 0; k < n1 && ok; k++) {
					if (k != i && orient2d(a, b, map->c[h1[k]].pos) < -EPS) ok = 0;
				}
				for (int k = 0; k < n2 && ok; k++) {
					if (k != j && orient2d(a, b, map->c[h2[k]].pos) < -EPS) ok = 0;
				}
				if (ok) {
					double d = dist2(a, b);
					if (d > best) {
						best = d;
						li = i;
						lj = j;
					}
				}
			}
		}
	}

	*bi = li;
	*bj = lj;
}

/* Merge two adjacent triangulations: stitch the gap between the hulls with the
 * rising-bubble seam (cross triangles from the lower to the upper tangent),
 * then splice the hull rings. Both rings are CCW. */
static void merge_triangulations(MapData *map, HullEdge *left_hull, HullEdge *right_hull,
                                 HullEdge **out_l, HullEdge **out_r)
{
	const double EPS = 1e-9;
	*out_l = NULL;
	*out_r = NULL;
	if (!left_hull) {
		*out_l = right_hull;
		*out_r = right_hull;
		return;
	}
	if (!right_hull) {
		*out_l = left_hull;
		*out_r = left_hull;
		return;
	}

	int c1 = ring_count(left_hull);
	int c2 = ring_count(right_hull);
	if (c1 < 2 || c2 < 2) {
		*out_l = left_hull;
		*out_r = left_hull;
		return;
	}

	int *h1 = malloc(sizeof(int) * (size_t)c1);
	int *h2 = malloc(sizeof(int) * (size_t)c2);
	int n1 = read_ring(left_hull, h1, c1);
	int n2 = read_ring(right_hull, h2, c2);

	int li, ri, ui, uj;
	lower_tangent(map, h1, n1, h2, n2, &li, &ri);
	upper_tangent(map, h1, n1, h2, n2, &ui, &uj);

	// Spliced hull (CCW): bridge li -> ri, around the outer (right) chain of the
	// right hull to uj, bridge uj -> ui, around the outer (left) chain of the
	// left hull back to li. All four tangent endpoints are extreme vertices and
	// must be included. Built BEFORE the bubble: it doubles as the merged-hull
	// membership test for the consumption lists below.
	int *ring = malloc(sizeof(int) * (size_t)(n1 + n2 + 1));
	int m = 0;
	ring[m++] = h1[li];
	int t = ri;
	for (;;) {
		ring[m++] = h2[t];
		if (t == uj) break;
		t = (t + 1) % n2;
	}
	t = ui;
	while (t != li) {
		ring[m++] = h1[t];
		t = (t + 1) % n1;
	}

	// Consumption lists: chain vertices the seam must visit, in order.
	// Normally the tangent-to-tangent arcs. But when a tangent pair coincides
	// (li == ui), the "arc" is empty while ring vertices strictly inside the
	// merged hull still need seam triangles (a vertex strictly inside its own
	// half's hull edge case, e.g. a point in the bay between the halves). Then
	// consume the full ring, skipping outer-boundary vertices (they already
	// belong to the merged hull and keep their sub-triangulation triangles).
	int *L = malloc(sizeof(int) * (size_t)(n1 + 1));
	int *R = malloc(sizeof(int) * (size_t)(n2 + 1));
	int nL = 0, nR = 0;
	if (li == ui) {
		L[nL++] = h1[li];
		t = (li + 1) % n1;
		while (t != li) {
			int v = h1[t];
			int on_hull = 0;
			for (int k = 0; k < m && !on_hull; k++) {
				if (ring[k] == v) on_hull = 1;
			}
			if (!on_hull) L[nL++] = v;
			t = (t + 1) % n1;
		}
		L[nL++] = h1[ui];
	} else {
		t = li;
		for (;;) {
			L[nL++] = h1[t];
			if (t == ui) break;
			t = (t + 1) % n1;
		}
	}
	if (ri == uj) {
		R[nR++] = h2[ri];
		t = (ri - 1 + n2) % n2;
		while (t != ri) {
			int v = h2[t];
			int on_hull = 0;
			for (int k = 0; k < m && !on_hull; k++) {
				if (ring[k] == v) on_hull = 1;
			}
			if (!on_hull) R[nR++] = v;
			t = (t - 1 + n2) % n2;
		}
		R[nR++] = h2[uj];
	} else {
		t = ri;
		for (;;) {
			R[nR++] = h2[t];
			if (t == uj) break;
			t = (t - 1 + n2) % n2;
		}
	}

	// Rising bubble: the bridge climbs from the lower to the upper tangent,
	// consuming one list vertex at a time. Left advances CCW (up its right
	// side), right advances CW (up its left side).
	int i = 0, j = 0;
	int guard = 0, maxguard = 4 * (nL + nR) + 16;
	while (guard++ < maxguard) {
		if (i == nL - 1 && j == nR - 1) break;

		int hasL = (i + 1 < nL);
		int hasR = (j + 1 < nR);
		Vector2 Pl = map->c[L[i]].pos;
		Vector2 Pr = map->c[R[j]].pos;
		Vector2 Pl2 = hasL ? map->c[L[i + 1]].pos : Pl;
		Vector2 Pr2 = hasR ? map->c[R[j + 1]].pos : Pr;

		int lok = hasL && (orient2d(Pl, Pr, Pl2) > -EPS);
		int rok = hasR && (orient2d(Pl, Pr, Pr2) > -EPS);
		if (!lok && !rok) break; // Should not happen before the lists are exhausted

		// NOTE: no "delete" skip of candidates here. Advancing one chain
		// vertex at a time keeps every seam triangle inside the strip, so the
		// strip is tiled without overlaps. Locally non-Delaunay diagonals are
		// repaired afterwards by the Lawson flip pass. (Skipping candidates
		// whose successor falls in the circle can jump over vertices that own
		// sub-triangulation triangles, emitting seam triangles on top of them.)
		if (lok && rok) {
			// The bridge stays Delaunay iff r2 is outside circle(l, r, l2).
			int use_right = (incircle(Pl, Pr, Pl2, Pr2) > EPS);
			// Guard: never emit a triangle that strictly swallows the other
			// candidate (it would overlap that side's sub-triangulation).
			// Mutual swallowing is impossible, so the override is always safe;
			// flips restore Delaunay afterwards.
			if (use_right) {
				if (point_in_tri(Pl2, Pl, Pr, Pr2)) use_right = 0;
			} else {
				if (point_in_tri(Pr2, Pl, Pr, Pl2)) use_right = 1;
			}
			if (use_right) {
				push_tri(map, L[i], R[j], R[j + 1]);
				j++;
			} else {
				push_tri(map, L[i], R[j], L[i + 1]);
				i++;
			}
		} else if (lok) {
			push_tri(map, L[i], R[j], L[i + 1]);
			i++;
		} else {
			push_tri(map, L[i], R[j], R[j + 1]);
			j++;
		}
	}

	free(L);
	free(R);

	// Wrap the splice into a CCW hull ring of hull edges.
	int leftmost = ring[0], rightmost = ring[0];
	for (int k = 1; k < m; k++) {
		Vector2 p = map->c[ring[k]].pos;
		Vector2 pl = map->c[leftmost].pos;
		Vector2 pr = map->c[rightmost].pos;
		if (p.x < pl.x || (p.x == pl.x && p.y < pl.y)) leftmost = ring[k];
		if (p.x > pr.x || (p.x == pr.x && p.y > pr.y)) rightmost = ring[k];
	}

	HullEdge *first = NULL, *prev = NULL;
	HullEdge *edge_l = NULL, *edge_r = NULL;
	for (int k = 0; k < m; k++) {
		HullEdge *e = new_hull(map, ring[k], ring[(k + 1) % m]);
		if (prev) {
			prev->next = e;
			e->prev = prev;
		} else {
			first = e;
		}
		prev = e;
		if (e->org == leftmost) edge_l = e;
		if (e->org == rightmost) edge_r = e;
	}
	prev->next = first;
	first->prev = prev;
	*out_l = edge_l ? edge_l : first;
	*out_r = edge_r ? edge_r : first;

	free(h1);
	free(h2);
	free(ring);
}

static void triangulate(MapData *map, int start, int end, HullEdge **left_e, HullEdge **right_e) {
  *left_e = NULL;
  *right_e = NULL;
  int n = end - start;
  if (n <= 1) return;

  if (n == 2) {
    int i = start, j = start + 1;
    HullEdge *e1 = new_hull(map, i, j);
    HullEdge *e2 = new_hull(map, j, i);
    e1->opposite = e2;
    e2->opposite = e1;
    e1->next = e2; e2->prev = e1;
    e2->next = e1; e1->prev = e2;
    *left_e = e1;
    *right_e = e2;
    return;
  }

  if (n == 3) {
    int i = start, j = start + 1, k = start + 2;
    Vector2 A = map->c[i].pos;
    Vector2 B = map->c[j].pos;
    Vector2 C = map->c[k].pos;
    HullEdge *a, *b, *cc;
    if (orient2d(A, B, C) >= 0) {
      a = new_hull(map, i, j);
      b = new_hull(map, j, k);
      cc = new_hull(map, k, i);
    } else {
      a = new_hull(map, i, k);
      b = new_hull(map, k, j);
      cc = new_hull(map, j, i);
    }
    a->next = b; b->prev = a;
    b->next = cc; cc->prev = b;
    cc->next = a; a->prev = cc;
    push_tri(map, i, j, k); // No-op when the three sites are collinear
    *left_e = (a->org == i) ? a : ((b->org == i) ? b : cc);
    *right_e = (a->org == k) ? a : ((b->org == k) ? b : cc);
    return;
  }

  int mid = start + n / 2;
  HullEdge *ll, *lr, *rl, *rr;
  triangulate(map, start, mid, &ll, &lr);
  triangulate(map, mid, end, &rl, &rr);
  (void)lr;
  (void)rr;
  merge_triangulations(map, ll, rl, left_e, right_e);
}

MapData *init_voronoi_map(Params p) {

  MapData *map = malloc(sizeof(MapData));
  map->nc = p.n_sites > 0 ? p.n_sites : 0;
  map->texture_size = p.map_size;

  map->cap_c = map->nc > 0 ? map->nc : 1;
  map->c = calloc((size_t)map->cap_c, sizeof(Center));

  for (int i = 0; i < map->nc; i++) {
    map->c[i].pos = (Vector2) { GetRandomValue(0, (int)p.map_size.x),
                                GetRandomValue(0, (int)p.map_size.y) };
  }

  map->nq = 0;
  map->q = NULL;
  map->ne = 0;
  map->e = NULL;
  map->spacing = 0;
  map->pool = NULL;
  map->poolN = 0;
  map->poolCap = 0;
  map->pstart = NULL;
  map->plen = NULL;

  map->tris = NULL;
  map->n_tris = 0;
  map->cap_tris = 0;
  map->howned = NULL;
  map->n_howned = 0;
  map->cap_howned = 0;
  map->clamped = 0;
  map->eidx = NULL;
  map->ekey = NULL;
  map->eicap = 0;
  map->eitomb = 0;
  map->ecap = 0;

  map->tex = (Texture2D){ 0 };
  map->dirty = 1;

  map->blob_power = 0.97f;

  return map;
}

static void free_graph(MapData *map)
{
	free(map->q);
	free(map->e);
	free(map->pool);
	free(map->pstart);
	free(map->plen);
	map->nq = 0;
	map->ne = 0;
	map->q = NULL;
	map->e = NULL;
	map->pool = NULL;
	map->pstart = NULL;
	map->plen = NULL;
	map->poolN = 0;
	map->poolCap = 0;
	map->ecap = 0;   /* e[] is freed above, so its capacity must reset too */
	map->spacing = 0;
}

void free_voronoi_map(MapData *map) {
  if (!map) return;
  if (map->tex.id != 0) {
    UnloadTexture(map->tex);
    map->tex = (Texture2D){ 0 };
  }
  free_graph(map);
  edge_index_free(map);
  free_hull_edges(map);
  free(map->tris);
  free(map->c);
  free(map);
}

/* ---------------------------------------------------------------------
 * Directed-edge hash index. A triangle owns three directed edges,
 * (v0,v1), (v1,v2) and (v2,v0); two triangles are adjacent exactly when
 * one owns (a,b) and the other owns (b,a). Indexing that turns
 * find_partner() from an O(n_tris) scan into an O(1) probe, which is what
 * takes the repair passes from quadratic to roughly linear.
 * ------------------------------------------------------------------ */

static unsigned eg_hash(unsigned long long k)
{
	k ^= k >> 33; k *= 0xff51afd7ed558ccdULL;
	k ^= k >> 33; k *= 0xc4ceb9fe1a85ec53ULL;
	k ^= k >> 33;
	return (unsigned)k;
}

static unsigned long long eg_pack(int a, int b)
{
	return ((unsigned long long)(unsigned)a << 32) | (unsigned)b;
}

static void eg_compact(MapData *map, int force);

/* -1 is a never-used slot and ends a probe; -2 is a tombstone and does not,
 * so unlinking one key never orphans whatever sits behind it in the chain. */
static void eg_add(MapData *map, unsigned long long k, int t)
{
	unsigned mask = (unsigned)map->eicap - 1;
	unsigned h = eg_hash(k) & mask;
	unsigned tries = 0;
	while (map->eidx[h] != -1) {
		if (++tries >= (unsigned)map->eicap) {
			eg_compact(map, 1);
			eg_add(map, k, t);
			return;
		}
		h = (h + 1) & mask;
	}
	map->eidx[h] = t;
	map->ekey[h] = k;
}

static void eg_drop_key(MapData *map, unsigned long long k)
{
	unsigned mask = (unsigned)map->eicap - 1;
	unsigned h = eg_hash(k) & mask;
	while (map->eidx[h] != -1) {
		if (map->eidx[h] >= 0 && map->ekey[h] == k) {
			map->eidx[h] = -2;
			map->eitomb++;
			return;
		}
		h = (h + 1) & mask;
	}
}

/* Every Lawson flip unlinks as many keys as it links, and splitting edges
 * grows n_tris, so the table has to be able to re-pack and grow itself.
 * Sizing it once up front overflows it part-way through the repair. */
static void eg_compact(MapData *map, int force)
{
	if (!force && map->eitomb <= map->eicap / 8) return;
	int old = map->eicap;
	int live = 0;
	for (int i = 0; i < old; i++) if (map->eidx[i] >= 0) live++;
	int cap = old;
	while (cap < 4 * (live + 1)) cap <<= 1;

	int n = live > 0 ? live : 1;
	unsigned long long *k = malloc(sizeof(*k) * (size_t)n);
	int *v = malloc(sizeof(*v) * (size_t)n);
	n = 0;
	for (int i = 0; i < old; i++)
		if (map->eidx[i] >= 0) { k[n] = map->ekey[i]; v[n] = map->eidx[i]; n++; }
	if (cap != old) {
		map->eidx = realloc(map->eidx, sizeof(int) * (size_t)cap);
		map->ekey = realloc(map->ekey, sizeof(unsigned long long) * (size_t)cap);
		map->eicap = cap;
	}
	for (int i = 0; i < cap; i++) map->eidx[i] = -1;
	map->eitomb = 0;
	for (int i = 0; i < n; i++) eg_add(map, k[i], v[i]);
	free(k);
	free(v);
}

static void edge_index_rebuild(MapData *map)
{
	int n = map->n_tris;
	int cap = 32;
	while (cap < 4 * (n > 0 ? n : 1)) cap <<= 1;   /* load factor <= 0.25 */
	if (cap != map->eicap) {
		map->eidx = realloc(map->eidx, sizeof(int) * (size_t)cap);
		map->ekey = realloc(map->ekey, sizeof(unsigned long long) * (size_t)cap);
		map->eicap = cap;
	}
	for (int i = 0; i < cap; i++) map->eidx[i] = -1;
	map->eitomb = 0;
	for (int t = 0; t < n; t++) {
		int *tr = &map->tris[3 * t];
		for (int e = 0; e < 3; e++) eg_add(map, eg_pack(tr[e], tr[(e + 1) % 3]), t);
	}
}

static void edge_index_free(MapData *map)
{
	free(map->eidx);
	free(map->ekey);
	map->eidx = NULL;
	map->ekey = NULL;
	map->eicap = 0;
	map->eitomb = 0;
}

/* Register / unregister one triangle's three directed edges. */
static void eg_link(MapData *map, int t)
{
	int *tr = &map->tris[3 * t];
	for (int e = 0; e < 3; e++) eg_add(map, eg_pack(tr[e], tr[(e + 1) % 3]), t);
}

static void eg_unlink(MapData *map, int t)
{
	int *tr = &map->tris[3 * t];
	for (int e = 0; e < 3; e++) eg_drop_key(map, eg_pack(tr[e], tr[(e + 1) % 3]));
}

/* Find a partner triangle sharing the directed edge b -> a (i.e. we hold a -> b). */
static int find_partner(MapData *map, int ti, int a, int b)
{
	(void)ti;
	if (!map->eidx) return -1;
	unsigned long long k = eg_pack(b, a);
	unsigned mask = (unsigned)map->eicap - 1;
	unsigned h = eg_hash(k) & mask;
	while (map->eidx[h] != -1) {
		if (map->eidx[h] >= 0 && map->ekey[h] == k) return map->eidx[h];
		h = (h + 1) & mask;
	}
	return -1;
}

/* Split edge e of triangle t at site s, keeping the triangle on the far side
 * conforming, and keep the edge index in step. */
static int split_edge_with_site(MapData *map, int t, int e, int s)
{
	int a = map->tris[3 * t + e];
	int b = map->tris[3 * t + (e + 1) % 3];
	int w = map->tris[3 * t + (e + 2) % 3];
	if (a == s || b == s || w == s) return 0;

	int pj = find_partner(map, t, a, b);
	int q = -1;
	if (pj >= 0 && pj != t) {
		int *nt = &map->tris[3 * pj];
		for (int k = 0; k < 3; k++) if (nt[k] != a && nt[k] != b) q = nt[k];
	}

	eg_unlink(map, t);
	map->tris[3 * t + 0] = a;
	map->tris[3 * t + 1] = s;
	map->tris[3 * t + 2] = w;
	int nb = map->n_tris;
	push_tri(map, s, b, w);
	eg_link(map, t);
	for (int i = nb; i < map->n_tris; i++) eg_link(map, i);

	if (pj >= 0 && pj != t && q >= 0) {
		eg_unlink(map, pj);
		map->tris[3 * pj + 0] = b;
		map->tris[3 * pj + 1] = s;
		map->tris[3 * pj + 2] = q;
		nb = map->n_tris;
		push_tri(map, s, a, q);
		eg_link(map, pj);
		for (int i = nb; i < map->n_tris; i++) eg_link(map, i);
	}
	return 1;
}

/* Point strictly inside CCW triangle (a, b, c)? */
static int point_in_tri(Vector2 p, Vector2 a, Vector2 b, Vector2 c)
{
	const double EPS = 1e-9;
	return orient2d(a, b, p) > EPS &&
	       orient2d(b, c, p) > EPS &&
	       orient2d(c, a, p) > EPS;
}

/* Point strictly between a and b (collinearity checked by caller)? */
static int point_on_segment(Vector2 p, Vector2 a, Vector2 b)
{
	const double EPS = 1e-9;
	double dx = b.x - a.x, dy = b.y - a.y;
	double len2 = dx * dx + dy * dy;
	if (len2 < EPS) return 0;
	double t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2;
	return t > EPS && t < 1.0 - EPS;
}

/* ---------------------------------------------------------------------
 * Triangle grid for the T-junction repair. A site lying on a triangle edge
 * is necessarily inside that triangle's bounding box, so registering each
 * triangle in every cell its bbox covers and consulting only the cell a site
 * falls in turns up exactly the candidates the old site-major
 * O(nc * n_tris) sweep did, without testing the other few thousand.
 * ------------------------------------------------------------------ */

typedef struct {
	float inv_g, W, H;
	int gw, gh;
	int *head;      /* cell -> first entry, or -1                  */
	int *tnext;     /* entry -> next entry, or -1                  */
	int *tri;       /* entry -> triangle index                     */
	int n, cap;
} TriGrid;

/* Cell size ~ mean site spacing, so a cell holds about one site and a
 * typical triangle covers a handful of cells. */
static void tg_geom(TriGrid *g, MapData *map)
{
	int n = map->nc;
	g->W = map->texture_size.x;
	g->H = map->texture_size.y;
	float gsz = sqrtf((g->W * g->H) / (float)(n > 0 ? n : 1));
	if (!(gsz > 0.0f)) gsz = 1.0f;
	g->inv_g = 1.0f / gsz;
	g->gw = (int)(g->W * g->inv_g) + 2;
	g->gh = (int)(g->H * g->inv_g) + 2;
}

static void tg_init(TriGrid *g, MapData *map)
{
	tg_geom(g, map);
	int ncells = g->gw * g->gh;
	g->head = malloc(sizeof(int) * (size_t)ncells);
	for (int i = 0; i < ncells; i++) g->head[i] = -1;
	g->tnext = NULL;
	g->tri = NULL;
	g->n = 0;
	g->cap = 0;
}

static void tg_free(TriGrid *g)
{
	free(g->head);
	free(g->tnext);
	free(g->tri);
	g->head = g->tnext = g->tri = NULL;
	g->n = g->cap = 0;
}

static int tg_cell(TriGrid *g, Vector2 p)
{
	int cx = (int)(p.x * g->inv_g) + 1;
	int cy = (int)(p.y * g->inv_g) + 1;
	if (cx < 0) cx = 0;
	if (cx >= g->gw) cx = g->gw - 1;
	if (cy < 0) cy = 0;
	if (cy >= g->gh) cy = g->gh - 1;
	return cy * g->gw + cx;
}

static void tg_push(TriGrid *g, int cell, int t)
{
	if (g->n == g->cap) {
		g->cap = g->cap ? g->cap * 2 : 1024;
		g->tnext = realloc(g->tnext, sizeof(int) * (size_t)g->cap);
		g->tri = realloc(g->tri, sizeof(int) * (size_t)g->cap);
	}
	g->tri[g->n] = t;
	g->tnext[g->n] = g->head[cell];
	g->head[cell] = g->n;
	g->n++;
}

/* Register triangle t in every cell its bounding box touches. A site lying
 * anywhere inside t (or on one of its edges) therefore shares t's cell. */
static void tg_push_tri(TriGrid *g, MapData *map, int t)
{
	int *tr = &map->tris[3 * t];
	float x0 = map->c[tr[0]].pos.x, x1 = x0;
	float y0 = map->c[tr[0]].pos.y, y1 = y0;
	for (int k = 1; k < 3; k++) {
		float px = map->c[tr[k]].pos.x, py = map->c[tr[k]].pos.y;
		if (px < x0) x0 = px;
		if (px > x1) x1 = px;
		if (py < y0) y0 = py;
		if (py > y1) y1 = py;
	}
	int cx0 = (int)(x0 * g->inv_g) + 1, cx1 = (int)(x1 * g->inv_g) + 1;
	int cy0 = (int)(y0 * g->inv_g) + 1, cy1 = (int)(y1 * g->inv_g) + 1;
	if (cx0 < 0) cx0 = 0;
	if (cy0 < 0) cy0 = 0;
	if (cx1 >= g->gw) cx1 = g->gw - 1;
	if (cy1 >= g->gh) cy1 = g->gh - 1;
	for (int cy = cy0; cy <= cy1; cy++)
		for (int cx = cx0; cx <= cx1; cx++)
			tg_push(g, cy * g->gw + cx, t);
}

static void tg_add_all(TriGrid *g, MapData *map)
{
	for (int t = 0; t < map->n_tris; t++) tg_push_tri(g, map, t);
}

/* The seam only walks hull chains, so a sub-hull vertex that ends up strictly
 * inside the merged hull (like a point in the "bay" between the halves) gets
 * no triangle. Insert every such site into its containing triangle. */
static void insert_missing_sites(MapData *map)
{
	const double EPS = 1e-9;
	int n = map->nc;
	char *used = calloc((size_t)(n > 0 ? n : 1), 1);

	// One sweep over the triangles marks every site that already owns one.
	// Asking "does any triangle mention site s?" once per site was O(nc*n_tris).
	for (int t = 0; t < map->n_tris; t++) {
		int *tr = &map->tris[3 * t];
		used[tr[0]] = 1;
		used[tr[1]] = 1;
		used[tr[2]] = 1;
	}

	// Grid over the triangles, kept up to date as sites are spliced in, so
	// locating the triangle that contains a stranded site is a cell lookup
	// instead of a scan over every triangle (which was O(nc*n_tris)).
	TriGrid grid;
	tg_init(&grid, map);
	tg_add_all(&grid, map);

	for (int s = 0; s < n; s++) {
		if (used[s]) continue;

		Vector2 p = map->c[s].pos;
		int done = 0;
		// Snapshot "next" before handling the entry: splitting pushes new
		// triangles onto this same cell list.
		for (int e = grid.head[tg_cell(&grid, p)]; e >= 0 && !done; ) {
			int nxt = grid.tnext[e];
			int t = grid.tri[e];
			int *tr = &map->tris[3 * t];
			if (tr[0] != s && tr[1] != s && tr[2] != s) {
				Vector2 a = map->c[tr[0]].pos;
				Vector2 b = map->c[tr[1]].pos;
				Vector2 cc = map->c[tr[2]].pos;

				if (point_in_tri(p, a, b, cc)) {
					int ia = tr[0], ib = tr[1], ic = tr[2];
					eg_unlink(map, t);
					tr[0] = ia; tr[1] = ib; tr[2] = s;
					int nb = map->n_tris;
					push_tri(map, ib, ic, s);
					push_tri(map, ic, ia, s);
					eg_link(map, t);
					for (int q = nb; q < map->n_tris; q++) {
						eg_link(map, q);
						tg_push_tri(&grid, map, q);
					}
					done = 1;
				} else {
					// On-edge split: p collinear on one triangle edge, strictly between.
					int ea[3] = { tr[0], tr[1], tr[2] };
					for (int k = 0; k < 3 && !done; k++) {
						Vector2 u = map->c[ea[k]].pos;
						Vector2 v = map->c[ea[(k + 1) % 3]].pos;
						if (fabs(orient2d(u, v, p)) < EPS &&
						    point_on_segment(p, u, v)) {
							int nb = map->n_tris;
							if (split_edge_with_site(map, t, k, s)) {
								for (int q = nb; q < map->n_tris; q++)
									tg_push_tri(&grid, map, q);
								done = 1;
							}
						}
					}
				}
			}
			e = nxt;
		}
		// If no containing triangle exists there is a coverage hole; leave the
		// site unused rather than corrupt the mesh (tests check for this).
		if (done) used[s] = 1;
	}

	free(used);

	// Enforce a conforming mesh: split EVERY site strictly on a non-incident
	// triangle edge (T-junction), whether or not the site already has
	// triangles. T-junctions break the simplicial-complex premise that
	// Delaunay's theorem and Lawson flips rely on: the site sits strictly
	// inside a circumcircle across an unflippable "edge", freezing violations
	// in place. Iterate to fixpoint. The old sweep tested every site against
	// every triangle; the grid in hand already narrows that to the few
	// triangles whose box covers the site.
	for (int pass = 0; pass < n + 2; pass++) {
		int split_any = 0;
		for (int s = 0; s < n; s++) {
			Vector2 p = map->c[s].pos;
			for (int e = grid.head[tg_cell(&grid, p)]; e >= 0; ) {
				int nxt = grid.tnext[e];
				int t = grid.tri[e];
				int *tr = &map->tris[3 * t];
				if (tr[0] != s && tr[1] != s && tr[2] != s) {
					int ea[3] = { tr[0], tr[1], tr[2] };
					for (int k = 0; k < 3; k++) {
						Vector2 u = map->c[ea[k]].pos;
						Vector2 v = map->c[ea[(k + 1) % 3]].pos;
						if (fabs(orient2d(u, v, p)) < EPS &&
						    point_on_segment(p, u, v)) {
							int nb = map->n_tris;
							if (split_edge_with_site(map, t, k, s)) {
								for (int q = nb; q < map->n_tris; q++)
									tg_push_tri(&grid, map, q);
								split_any = 1;
							}
							break; // tri t dissolved; move on (its pieces contain s)
						}
					}
				}
				e = nxt;
			}
		}
		if (!split_any) break;
	}

	tg_free(&grid);
}

/* Lawson flip pass: repeatedly flip non-Delaunay interior diagonals until the
 * triangulation is Delaunay. A full sweep is done before rescanning; restarting
 * the O(T^2) partner scan after every flip made the pass cubic. */
static void lawson_flip_pass(MapData *map)
{
	const double EPS = 1e-9;
	int guard = 0;
	int changed = 1;
	while (changed && guard++ < 100000) {
		changed = 0;
		for (int i = 0; i < map->n_tris; i++) {
			int *t = &map->tris[3 * i];
			for (int e = 0; e < 3; e++) {
				int a = t[e], b = t[(e + 1) % 3], p = t[(e + 2) % 3];
				int j = find_partner(map, i, a, b);
				if (j < 0 || j == i) continue;
				int *u = &map->tris[3 * j];
				int q = -1;
				for (int k = 0; k < 3; k++) {
					if (u[k] != a && u[k] != b) q = u[k];
				}
				if (q < 0) continue;
				Vector2 A = map->c[a].pos;
				Vector2 B = map->c[b].pos;
				Vector2 P = map->c[p].pos;
				Vector2 Q = map->c[q].pos;
				if (incircle(A, B, P, Q) > EPS) {
					// Quad b -> p -> a -> q; flip diagonal (a,b) to (p,q), but
					// only when the quad is strictly convex (all four left
					// turns), otherwise the flip would fold the mesh.
					if (orient2d(B, P, A) > EPS && orient2d(P, A, Q) > EPS &&
					    orient2d(A, Q, B) > EPS && orient2d(Q, B, P) > EPS) {
						eg_unlink(map, i);
						eg_unlink(map, j);
						t[0] = b; t[1] = p; t[2] = q;
						u[0] = p; u[1] = a; u[2] = q;
						eg_link(map, i);
						eg_link(map, j);
						eg_compact(map, 0);
						changed = 1;
					}
				}
			}
		}
	}
}

/* -------------------------------------------------------------------------
 * Voronoi graph
 * ---------------------------------------------------------------------- */

/* Circumcenter of a non-degenerate triangle (all stored tris qualify). */
static Vector2 circumcenter(Vector2 a, Vector2 b, Vector2 c)
{
	double d = 2.0 * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
	double a2 = a.x * a.x + a.y * a.y;
	double b2 = b.x * b.x + b.y * b.y;
	double c2 = c.x * c.x + c.y * c.y;
	Vector2 o;
	o.x = (float)((a2 * (b.y - c.y) + b2 * (c.y - a.y) + c2 * (a.y - b.y)) / d);
	o.y = (float)((a2 * (c.x - b.x) + b2 * (a.x - c.x) + c2 * (b.x - a.x)) / d);
	return o;
}

/* Clip segment p0-p1 to [0,W]x[0,H] (Liang-Barsky). */
static int clip_seg_rect(Vector2 p0, Vector2 p1, float W, float H, Vector2 *q0, Vector2 *q1)
{
	double dx = p1.x - p0.x, dy = p1.y - p0.y;
	double t0 = 0.0, t1 = 1.0;
	double p[4] = { -dx, dx, -dy, dy };
	double q[4] = { p0.x - 0.0, W - p0.x, p0.y - 0.0, H - p0.y };
	for (int i = 0; i < 4; i++) {
		if (fabs(p[i]) < 1e-12) {
			if (q[i] < 0) return 0;
		} else {
			double r = q[i] / p[i];
			if (p[i] < 0) {
				if (r > t1) return 0;
				if (r > t0) t0 = r;
			} else {
				if (r < t0) return 0;
				if (r < t1) t1 = r;
			}
		}
	}
	*q0 = (Vector2){ (float)(p0.x + dx * t0), (float)(p0.y + dy * t0) };
	*q1 = (Vector2){ (float)(p0.x + dx * t1), (float)(p0.y + dy * t1) };
	return 1;
}

/* Append v to a capped unique list. Returns 1 when added, 0 when already
 * present; on overflow it counts the drop in map->clamped and returns 0. */
static int add_unique(MapData *map, int *arr, int *n, int v)
{
	for (int k = 0; k < *n; k++) {
		if (arr[k] == v) return 0;
	}
	if (*n >= MAXN) {
		map->clamped++;
		return 0;
	}
	arr[(*n)++] = v;
	return 1;
}

static void pool_reserve(MapData *map, int need)
{
	if (need <= map->poolCap) return;
	int cap = map->poolCap ? map->poolCap : 128;
	while (cap < need) cap *= 2;
	map->pool = realloc(map->pool, sizeof(Vector2) * (size_t)cap);
	map->poolCap = cap;
}

static void push_edge(MapData *map, int d0, int d1, int v0, int v1)
{
	float W = map->texture_size.x, H = map->texture_size.y;

	if (map->ne == map->ecap) {
		int cap = map->ecap ? map->ecap * 2 : 256;
		map->e = realloc(map->e, sizeof(Edge) * (size_t)cap);
		map->pstart = realloc(map->pstart, sizeof(int) * (size_t)cap);
		map->plen = realloc(map->plen, sizeof(int) * (size_t)cap);
		map->ecap = cap;
	}

	int i = map->ne;
	Edge *e = &map->e[i];
	e->d0 = d0;
	e->d1 = d1;
	e->v0 = v0;
	e->v1 = v1;
	e->river = 0;

	// Geometry is straight for now: the polyline is the segment clipped to the
	// map rect. A future noise pass can emit more points here.
	Vector2 a = map->q[v0].pos, b = map->q[v1].pos;
	Vector2 c0, c1;
	if (clip_seg_rect(a, b, W, H, &c0, &c1)) {
		map->pstart[i] = map->poolN;
		map->plen[i] = 2;
		pool_reserve(map, map->poolN + 2);
		map->pool[map->poolN++] = c0;
		map->pool[map->poolN++] = c1;
		e->mid = (Vector2){ (c0.x + c1.x) / 2.0f, (c0.y + c1.y) / 2.0f };
	} else {
		map->pstart[i] = map->poolN;
		map->plen[i] = 0; // dual edge lies entirely outside the map
		e->mid = (Vector2){ (a.x + b.x) / 2.0f, (a.y + b.y) / 2.0f };
	}
	map->ne++;
}

/* Build the Voronoi graph from the Delaunay triangulation:
 *   corner per triangle (circumcenter, touching its 3 centers),
 *   one Edge per Delaunay edge, carrying both centers and both Voronoi corners,
 *   a synthetic border corner where a hull edge's dual ray meets the map rect.
 * Corner indices match triangle indices, so corner t belongs to triangle t. */
static void build_voronoi_graph(MapData *map)
{
	int n = map->nc;
	float W = map->texture_size.x, H = map->texture_size.y;

	free_graph(map);

	for (int i = 0; i < n; i++) {
		Center *c = &map->c[i];
		c->nneigh = 0;
		c->nfneigh = 0;
		c->nedges = 0;
		c->ncorners = 0;
		c->border = (c->pos.x <= 0.0f || c->pos.y <= 0.0f ||
		             c->pos.x >= W || c->pos.y >= H) ? 1 : 0;
	}

	// Corners: one per triangle.
	int qcap = map->n_tris > 0 ? map->n_tris : 1;
	map->q = calloc((size_t)(qcap + 3 * (map->n_tris > 0 ? map->n_tris : 1)), sizeof(Corner));
	map->nq = 0;
	for (int t = 0; t < map->n_tris; t++) {
		int *tr = &map->tris[3 * t];
		map->q[t].pos = circumcenter(map->c[tr[0]].pos, map->c[tr[1]].pos,
		                             map->c[tr[2]].pos);
		map->q[t].ntouch = 3;
		map->q[t].touches[0] = tr[0];
		map->q[t].touches[1] = tr[1];
		map->q[t].touches[2] = tr[2];
		map->q[t].border = (map->q[t].pos.x <= 0.0f || map->q[t].pos.y <= 0.0f ||
		                    map->q[t].pos.x >= W || map->q[t].pos.y >= H) ? 1 : 0;
	}
	map->nq = map->n_tris;

	// Edges: one per Delaunay edge, interior duals plus hull rays.
	for (int t = 0; t < map->n_tris; t++) {
		int *tr = &map->tris[3 * t];
		for (int e = 0; e < 3; e++) {
			int a = tr[e], b = tr[(e + 1) % 3];
			int j = find_partner(map, t, a, b);
			if (j > t) {
				push_edge(map, a, b, t, j); // dual segment between two corners
			} else if (j < 0) {
				// Hull edge: the dual is a ray from this corner outwards.
				Vector2 A = map->c[a].pos, B = map->c[b].pos;
				double dx = B.x - A.x, dy = B.y - A.y;
				double len = sqrt(dx * dx + dy * dy);
				if (len < 1e-12) continue;
				// Right of a CCW hull edge points away from the interior.
				Vector2 dir = { (float)(dy / len), (float)(-dx / len) };
				Vector2 O = map->q[t].pos;
				Vector2 far = { (float)(O.x + dir.x * (W + H) * 2),
				                 (float)(O.y + dir.y * (W + H) * 2) };
				Vector2 c0, c1;
				if (!clip_seg_rect(O, far, W, H, &c0, &c1)) continue;

				int v = map->nq++;
				map->q[v].pos = c1; // where the ray leaves the map
				map->q[v].ntouch = 2;
				map->q[v].touches[0] = a;
				map->q[v].touches[1] = b;
				map->q[v].border = 1;
				push_edge(map, a, b, t, v);
			}
		}
	}

	// Cross-links: corner prot/adj, center edges/neigh/corners.
	for (int i = 0; i < map->ne; i++) {
		Edge *e = &map->e[i];
		int vs[2] = { e->v0, e->v1 };
		int ds[2] = { e->d0, e->d1 };
		for (int k = 0; k < 2; k++) {
			int v = vs[k];
			add_unique(map, map->q[v].prot, &map->q[v].nprot, i);
			int other = vs[1 - k];
			add_unique(map, map->q[v].adj, &map->q[v].nadj, other);
		}
		for (int k = 0; k < 2; k++) {
			int d = ds[k];
			int other = ds[1 - k];
			add_unique(map, map->c[d].edges, &map->c[d].nedges, i);
			add_unique(map, map->c[d].fneigh, &map->c[d].nfneigh, other);
			/* A dual clipped away entirely (plen == 0) lies wholly outside the
			 * map, so these two cells share no boundary on screen: keep them out
			 * of the map's neighbour relation. */
			if (map->plen[i] > 0)
				add_unique(map, map->c[d].neigh, &map->c[d].nneigh, other);
			add_unique(map, map->c[d].corners, &map->c[d].ncorners, e->v0);
			add_unique(map, map->c[d].corners, &map->c[d].ncorners, e->v1);
		}
	}

	// Corners sorted by angle around their center, so they trace the cell.
	for (int d = 0; d < n; d++) {
		Center *c = &map->c[d];
		for (int k = 1; k < c->ncorners; k++) {
			int v = c->corners[k];
			double av = atan2((double)map->q[v].pos.y - c->pos.y,
			                  (double)map->q[v].pos.x - c->pos.x);
			int j = k - 1;
			while (j >= 0) {
				int w = c->corners[j];
				double aw = atan2((double)map->q[w].pos.y - c->pos.y,
				                   (double)map->q[w].pos.x - c->pos.x);
				if (aw <= av) break;
				c->corners[j + 1] = w;
				j--;
			}
			c->corners[j + 1] = v;
		}
	}

	// Average distance between neighboring centers.
	if (map->ne > 0) {
		double sum = 0;
		for (int i = 0; i < map->ne; i++) {
			sum += sqrt(dist2(map->c[map->e[i].d0].pos, map->c[map->e[i].d1].pos));
		}
		map->spacing = (float)(sum / map->ne);
	}
}

/* Triangulate the current centers and rebuild the whole Voronoi graph. */
static void triangulate_map(MapData *map) {
  if (!map) return;

  map->dirty = 1; // cached texture no longer matches the geometry
  free_hull_edges(map); // scratch from any previous run

  qsort(map->c, (size_t)map->nc, sizeof(Center), compare_sites);

  // Dedup: equal coordinates would break the merge (zero-length hull edges).
  int w = 0;
  for (int r = 0; r < map->nc; r++) {
    if (w > 0 &&
        map->c[r].pos.x == map->c[w - 1].pos.x &&
        map->c[r].pos.y == map->c[w - 1].pos.y) {
      continue;
    }
    if (w != r) map->c[w] = map->c[r];
    w++;
  }
  map->nc = w;

  // Reset previous results so the call is repeatable.
  map->n_tris = 0;
  map->clamped = 0;
  if (map->nc < 2) {
    build_voronoi_graph(map); // Still valid: 0-1 sites give trivial cells
    return;
  }

  HullEdge *left_e, *right_e;
  triangulate(map, 0, map->nc, &left_e, &right_e);

  // Repair: the D&C seam can strand sub-hull vertices that end up strictly
  // inside the merged hull; insert them, split T-junctions for conformity,
  // then flip back to Delaunay.
  edge_index_rebuild(map);
  insert_missing_sites(map);
  lawson_flip_pass(map);

  build_voronoi_graph(map);
}

/* Clip polygon against the half-plane dot(p - mid, nrm) <= 0. */
static int clip_halfplane(Vector2 *in, int nin, Vector2 *out, Vector2 mid, Vector2 nrm)
{
	const double EPS = 1e-9;
	int nout = 0;
	for (int i = 0; i < nin; i++) {
		Vector2 cur = in[i], nxt = in[(i + 1) % nin];
		double dc = (cur.x - mid.x) * nrm.x + (cur.y - mid.y) * nrm.y;
		double dn = (nxt.x - mid.x) * nrm.x + (nxt.y - mid.y) * nrm.y;
		int inC = dc <= EPS, inN = dn <= EPS;
		if (inC && inN) {
			out[nout++] = nxt;
		} else if (inC != inN) {
			double t = dc / (dc - dn);
			out[nout++] = (Vector2){ (float)(cur.x + (nxt.x - cur.x) * t),
			                         (float)(cur.y + (nxt.y - cur.y) * t) };
			if (!inC) out[nout++] = nxt;
		}
	}
	return nout;
}

int get_closest_center_index(MapData *map, Vector2 p) {
  /* dist2() returns a SQUARED distance, so the sentinel must be larger than
   * any possible one (max is width^2+height^2). The old value of
   * texture_size.x (~800) bailed out on points farther than ~28px from every
   * center and returned -1, which generate_heightmap then used to index c[-1]. */
  double minDist = 1e300;
  int minIndex = -1;
  for (int i = 0; i < map->nc; i++) {
    double dist = dist2(map->c[i].pos, p);
    if (dist < minDist) {
      minDist = dist;
      minIndex = i;
    }
  }
  return minIndex;
}

/* Point inside a center's (unclipped) cell? The Delaunay neighbors define it. */
static int point_in_cell(MapData *map, int ci, Vector2 p)
{
	Center *c = &map->c[ci];
	double dc = dist2(c->pos, p);
	double tol = dc * 1e-6 + 1e-3;
	for (int k = 0; k < c->nfneigh; k++) {
		if (dist2(map->c[c->fneigh[k]].pos, p) < dc - tol) return 0;
	}
	return 1;
}

/* Convex hull (monotone chain), CCW. Returns the vertex count in out. */
static int convex_hull(Vector2 *pts, int n, Vector2 *out, int cap)
{
	if (n <= 1) return 0;
	Vector2 *s = malloc(sizeof(Vector2) * (size_t)n);
	memcpy(s, pts, sizeof(Vector2) * (size_t)n);
	for (int i = 1; i < n; i++) { // insertion sort by (x, y)
		Vector2 v = s[i];
		int j = i - 1;
		while (j >= 0 && (s[j].x > v.x || (s[j].x == v.x && s[j].y > v.y))) {
			s[j + 1] = s[j];
			j--;
		}
		s[j + 1] = v;
	}
	// drop duplicates
	int u = 0;
	for (int i = 0; i < n; i++) {
		if (u == 0 || dist2(s[u - 1], s[i]) > 1e-8) s[u++] = s[i];
	}
	n = u;
	if (n <= 2) {
		for (int i = 0; i < n && i < cap; i++) out[i] = s[i];
		free(s);
		return n;
	}

	Vector2 *h = malloc(sizeof(Vector2) * (size_t)(2 * n));
	int m = 0;
	for (int pass = 0; pass < 2; pass++) {
		int start = m;
		for (int k = 0; k < n; k++) {
			int i = pass ? n - 1 - k : k;
			while (m - start >= 2 && orient2d(h[m - 2], h[m - 1], s[i]) <= 0) m--;
			h[m++] = s[i];
		}
		m--; // last point of this pass is the first of the next
	}
	free(s);
	if (m > cap) m = cap;
	for (int i = 0; i < m; i++) out[i] = h[i];
	free(h);
	return m < 3 ? 0 : m;
}

/* Cell polygon of a center: convex hull of the endpoints of its edges' clipped
 * polylines plus whichever rectangle corners actually fall inside the cell.
 * Every vertex of the clipped cell is either a Voronoi vertex (a polyline
 * endpoint), a bisector crossing the rectangle border (also an endpoint), or a
 * rectangle corner inside the cell; hulling those gives the exact polygon and
 * drops the endpoints that only lie along a straight side.
 * Needs room for about 2*nc points in out. */
static int cell_polygon(MapData *map, int ci, Vector2 *out, int cap)
{
	Center *c = &map->c[ci];
	float W = map->texture_size.x, H = map->texture_size.y;
	int n = 0;
	Vector2 corners[4] = { { 0, 0 }, { W, 0 }, { W, H }, { 0, H } };

	if (c->nedges == 0 && map->nc > 1) {
		// No triangulation to lean on (one or two centers): clip the rectangle
		// against every other center's bisector directly.
		Vector2 *a = malloc(sizeof(Vector2) * 4);
		Vector2 *b = malloc(sizeof(Vector2) * (size_t)(map->nc + 4));
		int nin = 4;
		for (int k = 0; k < 4; k++) a[k] = corners[k];
		Vector2 *inB = a, *outB = b;
		for (int o = 0; o < map->nc && nin >= 3; o++) {
			if (o == ci) continue;
			Vector2 op = map->c[o].pos;
			Vector2 mid = { (c->pos.x + op.x) / 2.0f, (c->pos.y + op.y) / 2.0f };
			Vector2 nrm = { op.x - c->pos.x, op.y - c->pos.y };
			nin = clip_halfplane(inB, nin, outB, mid, nrm);
			Vector2 *tmp = inB;
			inB = outB;
			outB = tmp;
		}
		n = nin > cap ? cap : nin;
		for (int k = 0; k < n; k++) out[k] = inB[k];
		free(a);
		free(b);
		return n < 3 ? 0 : n;
	}

	for (int k = 0; k < c->nedges && n < cap; k++) {
		int ei = c->edges[k];
		int s = map->pstart[ei], len = map->plen[ei];
		for (int j = 0; j < len && n < cap; j++) out[n++] = map->pool[s + j];
	}
	for (int k = 0; k < 4 && n < cap; k++) {
		if (point_in_cell(map, ci, corners[k])) out[n++] = corners[k];
	}

	return convex_hull(out, n, out, cap);
}

/* One Lloyd step: move every center to the centroid of its cell, then rebuild.
 * Evenly spreads the centers, which is what makes the cells map well. */
static void relax_one(MapData *map)
{
	int n = map->nc;
	if (n <= 0) return;
	int cap = 2 * n + 8;
	Vector2 *buf = malloc(sizeof(Vector2) * (size_t)cap);
	for (int i = 0; i < n; i++) {
		int m = cell_polygon(map, i, buf, cap);
		if (m < 3) continue; // cell not visible in the map rect, keep it put
		double sx = 0, sy = 0;
		for (int k = 0; k < m; k++) {
			sx += buf[k].x;
			sy += buf[k].y;
		}
		map->c[i].pos = (Vector2){ (float)(sx / m), (float)(sy / m) };
	}
	free(buf);
	triangulate_map(map);
}

void relax_voronoi_map(MapData *map, int iterations) {
  for (int i = 0; i < iterations; i++) relax_one(map);
}

// Make Cell type either water, land, ocean
void generate_heightmap(MapData *map) {
  if (map->nc <= 0) return;

  for (int i = 0; i < map->nc; i++) {
    map->c[i].elevation = 0; // Base height (ocean)
  }

  //Normal Continent
  add_hills(map, (int[]){8, 12}, (int[]){50, 80}, (int[]){20, map->texture_size.x/2}, (int[]){50, map->texture_size.y-50});
  add_hills(map, (int[]){8, 12}, (int[]){50, 80}, (int[]){map->texture_size.x/2, map->texture_size.x-20}, (int[]){50, map->texture_size.y-50});
  add_pits(map, (int[]){3, 5}, (int[]){60, 100}, (int[]){50, map->texture_size.x-50}, (int[]){50, map->texture_size.y-50});
  add_mount_range(map, (int[]){1, 4}, (int[]){8, 40}, (int[]){40, 80}, (int[]){50, map->texture_size.x/2}, (int[]){50, map->texture_size.y-50});
  add_mount_range(map, (int[]){1, 4}, (int[]){8, 40}, (int[]){40, 80}, (int[]){map->texture_size.x/2, map->texture_size.x-50}, (int[]){50, map->texture_size.y-50});
  add_through_range(map, (int[]){1, 2}, (int[]){8, 40}, (int[]){45, 50}, (int[]){50, map->texture_size.x-50}, (int[]){50, map->texture_size.y-50});
  add_strip(map, (int[]){30, 60}, (int[]){2, 6}, 0);
  multiply_elevation(map, 0.9, (int[]){80, 100});
  mask_elevation(map, 1.5f);
  // smooth_elevation(map, 1);
  // slop_elevation(map, 1.0f, GetRandomValue(0, 360));

  //Invert Miss
  // add_elevation(map, 40, 0);
  // mask_elevation(map, 0.2f);
  // add_hills(map, (int[]){10, 15}, (int[]){50, 80}, (int[]){50, map->texture_size.x-50}, (int[]){50, map->texture_size.y-50});
  // add_pits(map, (int[]){3, 5}, (int[]){60, 100}, (int[]){50, map->texture_size.x-50}, (int[]){50, map->texture_size.y-50});
  // add_mount_range(map, (int[]){1, 4}, (int[]){8, 40}, (int[]){40, 50}, (int[]){50, map->texture_size.x-50}, (int[]){50, map->texture_size.y-50});
  // add_through_range(map, (int[]){1, 2}, (int[]){8, 40}, (int[]){45, 50}, (int[]){50, map->texture_size.x-50}, (int[]){50, map->texture_size.y-50});
  // multiply_elevation(map, 0.8, (int[]){20, 100});
  // multiply_elevation(map, 0.7, (int[]){90, 100});
  // invert_elevation(map, 80);


  map->dirty = 1; // elevation drives the cell colors in the cached texture
}

/* Reseed the centers and rebuild the map, optionally relaxing afterwards. */
void generate_voronoi_map(MapData *map, Params p) {
  if (!map) return;

  map->texture_size = p.map_size;

  if (p.n_sites > map->cap_c) {
    map->c = realloc(map->c, sizeof(Center) * (size_t)p.n_sites);
    memset(map->c + map->cap_c, 0, sizeof(Center) * (size_t)(p.n_sites - map->cap_c));
    map->cap_c = p.n_sites;
  }
  map->nc = p.n_sites > 0 ? p.n_sites : 0;

  for (int i = 0; i < map->nc; i++) {
    map->c[i].pos = (Vector2) { GetRandomValue(0, (int)p.map_size.x),
                                GetRandomValue(0, (int)p.map_size.y) };
  }

  triangulate_map(map);
  if (p.relax > 0) relax_voronoi_map(map, p.relax);

  generate_heightmap(map);
}


/* Fill a convex polygon (cell outlines always are) into an Image.
 *
 * One span per scanline: each row intersects the polygon in exactly one
 * interval, so we only need the min/max crossing of the edges that span that
 * row, then write the run directly. That drops the per-pixel edge test the old
 * even-odd fill did (bbox x corners per pixel) and writes straight into the
 * pixel buffer instead of paying ImageDrawPixel per pixel.
 *
 * Rasterized at pixel centers (y + 0.5, x + 0.5): a pixel belongs to the span
 * when its center is inside, which makes adjacent cells share the boundary
 * pixel exactly once and keeps the coverage gap-free.
 */
static void fill_convex_poly(Image *img, const Vector2 *poly, int n, Color c)
{
	if (!img || !img->data || n < 3) return;

	int w = img->width, h = img->height;
	if (w <= 0 || h <= 0) return;

	float ymin = poly[0].y, ymax = poly[0].y;
	for (int i = 1; i < n; i++) {
		if (poly[i].y < ymin) ymin = poly[i].y;
		if (poly[i].y > ymax) ymax = poly[i].y;
	}

	// Rows whose center can fall inside the polygon.
	int ys = (int)ceilf(ymin - 0.5f);
	int ye = (int)floorf(ymax - 0.5f);
	if (ys < 0) ys = 0;
	if (ye > h - 1) ye = h - 1;

	Color *px = (Color *)img->data; // raylib 6.0 declares Image.data as void *
	for (int y = ys; y <= ye; y++) {
		float sy = (float)y + 0.5f;
		float xmin = 0, xmax = 0;
		int found = 0;

		for (int i = 0; i < n; i++) {
			Vector2 a = poly[i];
			Vector2 b = poly[(i + 1) % n];
			// Skip horizontal edges and edges entirely above/below this row.
			if ((a.y <= sy) == (b.y <= sy)) continue;
			float t = (sy - a.y) / (b.y - a.y);
			float x = a.x + (b.x - a.x) * t;
			if (!found) {
				xmin = xmax = x;
				found = 1;
			} else {
				if (x < xmin) xmin = x;
				if (x > xmax) xmax = x;
			}
		}
		if (!found) continue;

		// Round OUTWARD (floor the left, ceil the right). Neighbouring cells compute
		// the shared crossing by interpolating along their own edge, so the two
		// results can differ by a float epsilon. Rounding inward can then drop
		// the column between them and leave a 1px hole; rounding outward lets
		// them overlap by at most one column instead, and the later cell wins.
		int xa = (int)floorf(xmin - 0.5f);
		int xb = (int)ceilf(xmax - 0.5f);
		if (xa < 0) xa = 0;
		if (xb > w - 1) xb = w - 1;

		Color *row = px + (size_t)y * w;
		for (int x = xa; x <= xb; x++) row[x] = c;
	}
}

Image gen_voronoi_image(MapData *map) {
  Image image = GenImageColor((int)map->texture_size.x, (int)map->texture_size.y, WHITE);

  int cap = 2 * (map->nc > 0 ? map->nc : 1) + 8;
  Vector2 *poly = malloc(sizeof(Vector2) * (size_t)cap);
  for (int i = 0; i < map->nc; i++) {
    int n = cell_polygon(map, i, poly, cap);
    float elev = map->c[i].elevation / 100.0f;
    Color heightColor = get_percentage_gradient_color(heightmap_levels, heightmap_n, elev);
    
    // TODO Add option for this
    // if (elev < 0.2) heightColor = heightmap_levels[0].color;

    fill_convex_poly(&image, poly, n, heightColor);
  }
  free(poly);

  return image;
}

Texture2D gen_voronoi_texture(MapData *map) {
  if (!map->dirty && map->tex.id != 0) return map->tex;
  if (map->tex.id != 0) UnloadTexture(map->tex);
  Image img = gen_voronoi_image(map);
  map->tex = LoadTextureFromImage(img);
  UnloadImage(img);
  map->dirty = 0;
  return map->tex;
}

/* Screen overlay for the cached map: one textured quad, then vector lines and
 * dots. No per-pixel work here, which is what the old DrawPixel fill did. */
void draw_voronoi_map(MapData *map, int with_lines) {
  DrawTextureEx(gen_voronoi_texture(map), (Vector2){ 0.0f, 0.0f }, 0.0f, 1.0f, WHITE);

  if (!with_lines) return;

  // Cell borders: the already-clipped edge polylines.
  Color edge = { 100, 100, 100, 255 };
  for (int i = 0; i < map->ne; i++) {
    int s = map->pstart[i], len = map->plen[i];
    for (int j = 0; j + 1 < len; j++) {
      DrawLineV(map->pool[s + j], map->pool[s + j + 1], edge);
    }
  }

  // Sites on top
  for (int i = 0; i < map->nc; i++) {
    DrawCircleV(map->c[i].pos, 2, BLACK);
  }
}
