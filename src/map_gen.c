#include <raylib.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

typedef struct Edge {
  int org, dest; // Origin and destination site indices

  struct Edge *next, *prev; // Next/previous edge on the hull ring (CCW)
  struct Edge *opposite; // Opposite half-edge (only set for the trivial 2-point case)

  int idx; // Creation index (debug)

} Edge;

typedef struct {

  Vector2 pos;
  Color color;

} Site;

typedef struct {

  int n_cells; // == map->n_sites at build time (one cell per site)
  Vector2 **polys; // Per-site cell polygon, clipped to the texture rect
  int *poly_n;

  Vector2 *verts; // Voronoi vertices = triangle circumcenters, index-aligned with map->tris
  int n_verts; // == map->n_tris

  int *edges; // Flat index pairs into verts: dual of interior Delaunay edges
  int n_edges;

  Vector2 *border; // Flat point pairs: clipped border segments (dual of hull edges)
  int n_border;

} VoronoiDiagram;

typedef struct {

  Vector2 texture_size;

  int n_sites;
  Site *sites;

  int *tris; // Flat triplets of site indices, always CCW
  int n_tris, cap_tris;

  Edge **owned; // Every allocated edge, for cleanup
  int n_owned, cap_owned;

  VoronoiDiagram vor; // Dual diagram + cells, rebuilt by genVoronoiDiagram()

} VoronoiMap;

// Divide-and-conquer Delaunay triangulation (Lee/Preparata - Guibas/Stolfi style).
// Sites must be sorted by x (then y). triangulate() handles sites[start, end)
// and returns its convex hull as a CCW edge ring: *left_e is the hull edge leaving
// the leftmost site, *right_e the hull edge leaving the rightmost site.
// Finished triangles accumulate in map->tris.

static int compare_sites(const void *a_v, const void *b_v)
{

	Site *a = (Site *) a_v;
	Site *b = (Site *) b_v;

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

// Forward declarations (defined with the post-pass / diagram helpers below).
static int point_in_tri(Vector2 p, Vector2 a, Vector2 b, Vector2 c);
static void freeVoronoiDiagram(VoronoiDiagram *d);

static double dist2(Vector2 a, Vector2 b)
{
	double dx = a.x - b.x;
	double dy = a.y - b.y;
	return dx * dx + dy * dy;
}

// > 0 iff d lies strictly inside the circumcircle of CCW triangle (a, b, c).
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

static Edge *new_edge(VoronoiMap *map, int org, int dest)
{
	if (map->n_owned == map->cap_owned) {
		map->cap_owned = map->cap_owned ? map->cap_owned * 2 : 64;
		map->owned = realloc(map->owned, sizeof(Edge *) * (size_t)map->cap_owned);
	}

	Edge *e = malloc(sizeof(Edge));
	e->org = org;
	e->dest = dest;
	e->next = e->prev = NULL;
	e->opposite = NULL;
	e->idx = map->n_owned;
	map->owned[map->n_owned++] = e;
	return e;
}

// Store triangle (a, b, c); enforces CCW order, drops degenerate (collinear) ones.
static void push_tri(VoronoiMap *map, int a, int b, int c)
{
	double o = orient2d(map->sites[a].pos, map->sites[b].pos, map->sites[c].pos);
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

static int ring_count(Edge *start)
{
	int n = 0;
	Edge *e = start;
	if (!e) return 0;
	do {
		n++;
		e = e->next;
		if (n > 1000000) break; // Safety against a corrupted ring
	} while (e && e != start);
	return n;
}

static int read_ring(Edge *start, int *out, int cap)
{
	int n = 0;
	Edge *e = start;
	if (!e) return 0;
	do {
		if (n >= cap) break;
		out[n++] = e->org;
		e = e->next;
	} while (e && e != start);
	return n;
}

// NOTE on collinear hull points: they are deliberately KEPT in the hull rings.
// The seam bubble steps through them one at a time (emitting degenerate
// triangles that are skipped), which subdivides the strip instead of emitting
// giant spanning triangles. Tangent selection picks extreme endpoints among
// same-slope ties, stranded middles are reinserted by insert_missing_sites(),
// and T-junctions are split to keep the mesh conforming.

// Lower common tangent of the left hull (h1) and right hull (h2), directed h1 -> h2.
// Correct selection: among all pairs with every site on the left, the tangent
// minimizes the slope (steepest downward line that keeps everything above).
// (Longest-valid is NOT the tangent: it can touch a hull off its lower chain.)
static void lower_tangent(VoronoiMap *map, int *h1, int n1, int *h2, int n2, int *bi, int *bj)
{
	const double EPS = 1e-9;
	int found = 0;
	int li = 0, lj = 0;
	double bdy = 0, bdx = 1, bestd = -1;

	for (int i = 0; i < n1; i++) {
		for (int j = 0; j < n2; j++) {
			Vector2 a = map->sites[h1[i]].pos;
			Vector2 b = map->sites[h2[j]].pos;
			double dx = b.x - a.x;
			if (dx <= EPS) continue; // Strictly separated in x (sorted split)
			int ok = 1;
			for (int k = 0; k < n1 && ok; k++) {
				if (k != i && orient2d(a, b, map->sites[h1[k]].pos) < -EPS) ok = 0;
			}
			for (int k = 0; k < n2 && ok; k++) {
				if (k != j && orient2d(a, b, map->sites[h2[k]].pos) < -EPS) ok = 0;
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
				Vector2 a = map->sites[h1[i]].pos;
				Vector2 b = map->sites[h2[j]].pos;
				int ok = 1;
				for (int k = 0; k < n1 && ok; k++) {
					if (k != i && orient2d(a, b, map->sites[h1[k]].pos) < -EPS) ok = 0;
				}
				for (int k = 0; k < n2 && ok; k++) {
					if (k != j && orient2d(a, b, map->sites[h2[k]].pos) < -EPS) ok = 0;
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

// Upper common tangent, directed h2 -> h1 (right-to-left along the top).
// Mirror image: among all pairs with every site on the left, maximize slope.
static void upper_tangent(VoronoiMap *map, int *h1, int n1, int *h2, int n2, int *bi, int *bj)
{
	const double EPS = 1e-9;
	int found = 0;
	int li = 0, lj = 0;
	double bdy = 0, bdx = 1, bestd = -1;

	for (int i = 0; i < n1; i++) {
		for (int j = 0; j < n2; j++) {
			Vector2 a = map->sites[h2[j]].pos;
			Vector2 b = map->sites[h1[i]].pos;
			double dx = b.x - a.x;
			if (dx >= -EPS) continue; // Must head strictly westward
			int ok = 1;
			for (int k = 0; k < n1 && ok; k++) {
				if (k != i && orient2d(a, b, map->sites[h1[k]].pos) < -EPS) ok = 0;
			}
			for (int k = 0; k < n2 && ok; k++) {
				if (k != j && orient2d(a, b, map->sites[h2[k]].pos) < -EPS) ok = 0;
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
				Vector2 a = map->sites[h2[j]].pos;
				Vector2 b = map->sites[h1[i]].pos;
				int ok = 1;
				for (int k = 0; k < n1 && ok; k++) {
					if (k != i && orient2d(a, b, map->sites[h1[k]].pos) < -EPS) ok = 0;
				}
				for (int k = 0; k < n2 && ok; k++) {
					if (k != j && orient2d(a, b, map->sites[h2[k]].pos) < -EPS) ok = 0;
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

// Merge two adjacent triangulations: stitch the gap between the hulls with the
// rising-bubble seam (cross triangles from the lower to the upper tangent),
// then splice the hull rings. Both rings are CCW.
static void merge_triangulations(VoronoiMap *map, Edge *left_hull, Edge *right_hull,
                                 Edge **out_l, Edge **out_r)
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

	if (getenv("TRI_DEBUG")) {
		fprintf(stderr, "merge n1=%d n2=%d h1=[", n1, n2);
		for (int k = 0; k < n1; k++) fprintf(stderr, "%d ", h1[k]);
		fprintf(stderr, "] h2=[");
		for (int k = 0; k < n2; k++) fprintf(stderr, "%d ", h2[k]);
		fprintf(stderr, "] lower=(%d,%d) upper=(%d,%d)\n",
		        h1[li], h2[ri], h1[ui], h2[uj]);
	}

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
		Vector2 Pl = map->sites[L[i]].pos;
		Vector2 Pr = map->sites[R[j]].pos;
		Vector2 Pl2 = hasL ? map->sites[L[i + 1]].pos : Pl;
		Vector2 Pr2 = hasR ? map->sites[R[j + 1]].pos : Pr;

		int lok = hasL && (orient2d(Pl, Pr, Pl2) > -EPS);
		int rok = hasR && (orient2d(Pl, Pr, Pr2) > -EPS);
		if (!lok && !rok) {
			if (getenv("TRI_DEBUG")) {
				fprintf(stderr, "  stall: i=%d/%d j=%d/%d (bridge %d->%d)\n",
				        i, nL, j, nR, L[i], R[j]);
			}
			break; // Should not happen before the lists are exhausted
		}

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

	int leftmost = ring[0], rightmost = ring[0];
	for (int k = 1; k < m; k++) {
		Vector2 p = map->sites[ring[k]].pos;
		Vector2 pl = map->sites[leftmost].pos;
		Vector2 pr = map->sites[rightmost].pos;
		if (p.x < pl.x || (p.x == pl.x && p.y < pl.y)) leftmost = ring[k];
		if (p.x > pr.x || (p.x == pr.x && p.y > pr.y)) rightmost = ring[k];
	}

	Edge *first = NULL, *prev = NULL;
	Edge *edge_l = NULL, *edge_r = NULL;
	for (int k = 0; k < m; k++) {
		Edge *e = new_edge(map, ring[k], ring[(k + 1) % m]);
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

void triangulate(VoronoiMap *map, int start, int end, Edge **left_e, Edge **right_e) {
  *left_e = NULL;
  *right_e = NULL;
  int n = end - start;
  if (n <= 1) return;

  if (n == 2) {
    int i = start, j = start + 1;
    Edge *e1 = new_edge(map, i, j);
    Edge *e2 = new_edge(map, j, i);
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
    Vector2 A = map->sites[i].pos;
    Vector2 B = map->sites[j].pos;
    Vector2 C = map->sites[k].pos;
    Edge *a, *b, *c;
    if (orient2d(A, B, C) >= 0) {
      a = new_edge(map, i, j);
      b = new_edge(map, j, k);
      c = new_edge(map, k, i);
    } else {
      a = new_edge(map, i, k);
      b = new_edge(map, k, j);
      c = new_edge(map, j, i);
    }
    a->next = b; b->prev = a;
    b->next = c; c->prev = b;
    c->next = a; a->prev = c;
    push_tri(map, i, j, k); // No-op when the three sites are collinear
    *left_e = (a->org == i) ? a : ((b->org == i) ? b : c);
    *right_e = (a->org == k) ? a : ((b->org == k) ? b : c);
    return;
  }

  int mid = start + n / 2;
  Edge *ll, *lr, *rl, *rr;
  triangulate(map, start, mid, &ll, &lr);
  triangulate(map, mid, end, &rl, &rr);
  (void)lr;
  (void)rr;
  merge_triangulations(map, ll, rl, left_e, right_e);
}

VoronoiMap* initVoronoiMap(Vector2 size, int n_sites) {

  VoronoiMap *map = malloc(sizeof(VoronoiMap));
  map->n_sites = n_sites;
  map->texture_size = size;

  map->sites = malloc(sizeof(Site) * (size_t)n_sites);

  for (int i = 0; i < n_sites; i++) {
    map->sites[i].pos = (Vector2) { GetRandomValue(0, size.x), GetRandomValue(0, size.y) };
    if (GetRandomValue(0, 2) == 0) {
      map->sites[i].color = (Color) { 50, 50, 255, 255 };
    } else {
      map->sites[i].color = (Color) { 50, 255, 50, 255 };
    }
  }

  map->tris = NULL;
  map->n_tris = 0;
  map->cap_tris = 0;

  map->owned = NULL;
  map->n_owned = 0;
  map->cap_owned = 0;

  map->vor.n_cells = 0;
  map->vor.polys = NULL;
  map->vor.poly_n = NULL;
  map->vor.verts = NULL;
  map->vor.n_verts = 0;
  map->vor.edges = NULL;
  map->vor.n_edges = 0;
  map->vor.border = NULL;
  map->vor.n_border = 0;

  return map;
}

void freeVoronoiMap(VoronoiMap *map) {
  if (!map) return;
  for (int i = 0; i < map->n_owned; i++) free(map->owned[i]);
  free(map->owned);
  free(map->tris);
  free(map->sites);
  freeVoronoiDiagram(&map->vor);
  free(map);
}

// Find a partner triangle sharing the directed edge b -> a (i.e. we hold a -> b).
static int find_partner(VoronoiMap *map, int ti, int a, int b)
{
	for (int j = 0; j < map->n_tris; j++) {
		if (j == ti) continue;
		int *t = &map->tris[3 * j];
		if ((t[0] == b && t[1] == a) ||
		    (t[1] == b && t[2] == a) ||
		    (t[2] == b && t[0] == a)) {
			return j;
		}
	}
	return -1;
}

// Point strictly inside CCW triangle (a, b, c)?
static int point_in_tri(Vector2 p, Vector2 a, Vector2 b, Vector2 c)
{
	const double EPS = 1e-9;
	return orient2d(a, b, p) > EPS &&
	       orient2d(b, c, p) > EPS &&
	       orient2d(c, a, p) > EPS;
}

// Point strictly between a and b (collinearity checked by caller)?
static int point_on_segment(Vector2 p, Vector2 a, Vector2 b)
{
	const double EPS = 1e-9;
	double dx = b.x - a.x, dy = b.y - a.y;
	double len2 = dx * dx + dy * dy;
	if (len2 < EPS) return 0;
	double t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2;
	return t > EPS && t < 1.0 - EPS;
}

// The seam only walks hull chains, so a sub-hull vertex that ends up strictly
// inside the merged hull (like a point in the "bay" between the halves) gets
// no triangle. Insert every such site into its containing triangle.
static void insert_missing_sites(VoronoiMap *map)
{
	const double EPS = 1e-9;
	int n = map->n_sites;
	char *used = calloc((size_t)(n > 0 ? n : 1), 1);

	for (int s = 0; s < n; s++) {
		if (used[s]) continue;
		int cnt = 0;
		for (int t = 0; t < map->n_tris; t++) {
			int *tr = &map->tris[3 * t];
			if (tr[0] == s || tr[1] == s || tr[2] == s) {
				cnt++;
				break;
			}
		}
		if (cnt) {
			used[s] = 1;
			continue;
		}

		Vector2 p = map->sites[s].pos;
		int done = 0;
		for (int t = 0; t < map->n_tris && !done; t++) {
			int *tr = &map->tris[3 * t];
			Vector2 a = map->sites[tr[0]].pos;
			Vector2 b = map->sites[tr[1]].pos;
			Vector2 c = map->sites[tr[2]].pos;

			if (point_in_tri(p, a, b, c)) {
				int ia = tr[0], ib = tr[1], ic = tr[2];
				tr[0] = ia; tr[1] = ib; tr[2] = s;
				push_tri(map, ib, ic, s);
				push_tri(map, ic, ia, s);
				done = 1;
			} else {
				// On-edge split: p collinear on one triangle edge, strictly between.
				int ea[3] = { tr[0], tr[1], tr[2] };
				for (int e = 0; e < 3 && !done; e++) {
					Vector2 u = map->sites[ea[e]].pos;
					Vector2 v = map->sites[ea[(e + 1) % 3]].pos;
					if (fabs(orient2d(u, v, p)) < EPS && point_on_segment(p, u, v)) {
						int w = ea[(e + 2) % 3];
						tr[0] = ea[e]; tr[1] = s; tr[2] = w;
						push_tri(map, s, ea[(e + 1) % 3], w);
						int pj = find_partner(map, t, ea[e], ea[(e + 1) % 3]);
						if (pj >= 0) {
							int *nt = &map->tris[3 * pj];
							int q = -1;
							for (int k = 0; k < 3; k++) {
								if (nt[k] != ea[e] && nt[k] != ea[(e + 1) % 3]) q = nt[k];
							}
							if (q >= 0) {
								nt[0] = ea[(e + 1) % 3]; nt[1] = s; nt[2] = q;
								push_tri(map, s, ea[e], q);
							}
						}
						done = 1;
					}
				}
			}
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
	// in place. Iterate to fixpoint (splits only add s as a vertex, so each
	// edge is split at most once per site; bounded).
	for (int pass = 0; pass < n + 2; pass++) {
		int split_any = 0;
		for (int s = 0; s < n; s++) {
			Vector2 p = map->sites[s].pos;
			for (int t = 0; t < map->n_tris; t++) {
				int *tr = &map->tris[3 * t];
				if (tr[0] == s || tr[1] == s || tr[2] == s) continue;
				int ea[3] = { tr[0], tr[1], tr[2] };
				for (int e = 0; e < 3; e++) {
					Vector2 u = map->sites[ea[e]].pos;
					Vector2 v = map->sites[ea[(e + 1) % 3]].pos;
					if (fabs(orient2d(u, v, p)) < EPS && point_on_segment(p, u, v)) {
						int w = ea[(e + 2) % 3];
						tr[0] = ea[e]; tr[1] = s; tr[2] = w;
						push_tri(map, s, ea[(e + 1) % 3], w);
						int pj = find_partner(map, t, ea[e], ea[(e + 1) % 3]);
						if (pj >= 0) {
							int *nt = &map->tris[3 * pj];
							int q = -1;
							for (int k = 0; k < 3; k++) {
								if (nt[k] != ea[e] && nt[k] != ea[(e + 1) % 3]) q = nt[k];
							}
							if (q >= 0) {
								nt[0] = ea[(e + 1) % 3]; nt[1] = s; nt[2] = q;
								push_tri(map, s, ea[e], q);
							}
						}
						split_any = 1;
						break; // tri t dissolved; move on (its pieces contain s)
					}
				}
			}
		}
		if (!split_any) break;
	}
}

// Lawson flip pass: repeatedly flip non-Delaunay interior diagonals until the
// triangulation is Delaunay. Terminates; the guard is belt and braces.
static void lawson_flip_pass(VoronoiMap *map)
{
	const double EPS = 1e-9;
	int guard = 0;
	int flips = 0;
	int changed = 1;
	while (changed && guard++ < 100000) {
		changed = 0;
		for (int i = 0; i < map->n_tris; i++) {
			int *t = &map->tris[3 * i];
			for (int e = 0; e < 3; e++) {
				int a = t[e], b = t[(e + 1) % 3], p = t[(e + 2) % 3];
				int j = find_partner(map, i, a, b);
				if (j < 0) continue;
				int *u = &map->tris[3 * j];
				int q = -1;
				for (int k = 0; k < 3; k++) {
					if (u[k] != a && u[k] != b) q = u[k];
				}
				if (q < 0) continue;
				Vector2 A = map->sites[a].pos;
				Vector2 B = map->sites[b].pos;
				Vector2 P = map->sites[p].pos;
				Vector2 Q = map->sites[q].pos;
				if (incircle(A, B, P, Q) > EPS) {
					// Quad b -> p -> a -> q; flip diagonal (a,b) to (p,q), but
					// only when the quad is strictly convex (all four left
					// turns), otherwise the flip would fold the mesh.
					if (orient2d(B, P, A) > EPS && orient2d(P, A, Q) > EPS &&
					    orient2d(A, Q, B) > EPS && orient2d(Q, B, P) > EPS) {
						t[0] = b; t[1] = p; t[2] = q;
						u[0] = p; u[1] = a; u[2] = q;
						changed = 1;
						flips++;
					}
				}
			}
		}
	}
	if (getenv("TRI_DEBUG")) {
		fprintf(stderr, "flip pass: %d flips, %d tris\n", flips, map->n_tris);
	}
}

// Circumcenter of a non-degenerate triangle (all stored tris qualify).
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

// Clip convex(?) polygon against the half-plane dot(p - mid, nrm) <= 0.
// Returns the vertex count of out (ping-pong buffers, cap >= nin + 1).
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
		} else if (inC && !inN) {
			double t = dc / (dc - dn);
			out[nout++] = (Vector2){ (float)(cur.x + (nxt.x - cur.x) * t),
			                         (float)(cur.y + (nxt.y - cur.y) * t) };
		} else if (!inC && inN) {
			double t = dc / (dc - dn);
			out[nout++] = (Vector2){ (float)(cur.x + (nxt.x - cur.x) * t),
			                         (float)(cur.y + (nxt.y - cur.y) * t) };
			out[nout++] = nxt;
		}
	}
	return nout;
}

// Clip segment p0-p1 to [0,W]x[0,H] (Liang-Barsky). Returns 1 if any part is
// visible, writing the clipped endpoints.
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

// Even-odd scanline polygon fill.
static void image_fill_poly(Image *img, Vector2 *poly, int n, Color c)
{
	if (n < 3) return;
	float x0 = poly[0].x, x1 = poly[0].x, y0 = poly[0].y, y1 = poly[0].y;
	for (int i = 1; i < n; i++) {
		if (poly[i].x < x0) x0 = poly[i].x;
		if (poly[i].x > x1) x1 = poly[i].x;
		if (poly[i].y < y0) y0 = poly[i].y;
		if (poly[i].y > y1) y1 = poly[i].y;
	}
	int ix0 = x0 < 0 ? 0 : (int)x0, iy0 = y0 < 0 ? 0 : (int)y0;
	int ix1 = x1 >= img->width ? img->width - 1 : (int)x1;
	int iy1 = y1 >= img->height ? img->height - 1 : (int)y1;
	for (int y = iy0; y <= iy1; y++) {
		for (int x = ix0; x <= ix1; x++) {
			int inside = 0;
			for (int i = 0, k = n - 1; i < n; k = i++) {
				float px = poly[k].x, py = poly[k].y;
				float qx = poly[i].x, qy = poly[i].y;
				if ((py > y) != (qy > y)) {
					float xin = px + (qx - px) * (y - py) / (qy - py);
					if (x < xin) inside = !inside;
				}
			}
			if (inside) ImageDrawPixel(img, x, y, c);
		}
	}
}

static void freeVoronoiDiagram(VoronoiDiagram *d)
{
	if (!d) return;
	if (d->polys) {
		for (int i = 0; i < d->n_cells; i++) free(d->polys[i]);
		free(d->polys);
	}
	free(d->poly_n);
	free(d->verts);
	free(d->edges);
	free(d->border);
	d->n_cells = 0;
	d->polys = NULL;
	d->poly_n = NULL;
	d->verts = NULL;
	d->n_verts = 0;
	d->edges = NULL;
	d->n_edges = 0;
	d->border = NULL;
	d->n_border = 0;
}

// Build the Voronoi dual of the Delaunay triangulation plus exact cell
// polygons: cell(s) = texture rect clipped by every bisector half-plane
// keeping the side of s. Robust for all degeneracies (1-2 sites, collinear,
// cocircular); needs no triangulation topology for the cells.
void genVoronoiDiagram(VoronoiMap *map)
{
	if (!map) return;
	freeVoronoiDiagram(&map->vor);
	VoronoiDiagram *vd = &map->vor;

	float W = map->texture_size.x, H = map->texture_size.y;
	int n = map->n_sites;

	// Cells via half-plane intersection (Sutherland-Hodgman per bisector).
	vd->n_cells = n;
	if (n > 0) {
		vd->polys = malloc(sizeof(Vector2 *) * (size_t)n);
		vd->poly_n = malloc(sizeof(int) * (size_t)n);
		int cap = n + 8;
		Vector2 *bufA = malloc(sizeof(Vector2) * (size_t)cap);
		Vector2 *bufB = malloc(sizeof(Vector2) * (size_t)cap);
		for (int s = 0; s < n; s++) {
			Vector2 *inB = bufA, *outB = bufB;
			int nin = 4;
			inB[0] = (Vector2){ 0, 0 };
			inB[1] = (Vector2){ W, 0 };
			inB[2] = (Vector2){ W, H };
			inB[3] = (Vector2){ 0, H };
			Vector2 sp = map->sites[s].pos;
			for (int o = 0; o < n && nin >= 3; o++) {
				if (o == s) continue;
				Vector2 op = map->sites[o].pos;
				Vector2 mid = { (sp.x + op.x) / 2.0f, (sp.y + op.y) / 2.0f };
				Vector2 nrm = { op.x - sp.x, op.y - sp.y };
				nin = clip_halfplane(inB, nin, outB, mid, nrm);
				Vector2 *tmp = inB;
				inB = outB;
				outB = tmp;
			}
			vd->poly_n[s] = nin;
			vd->polys[s] = malloc(sizeof(Vector2) * (size_t)(nin > 0 ? nin : 1));
			for (int k = 0; k < nin; k++) vd->polys[s][k] = inB[k];
		}
		free(bufA);
		free(bufB);
	}

	// Dual vertices: circumcenters, index-aligned with map->tris.
	vd->n_verts = map->n_tris;
	if (map->n_tris > 0) {
		vd->verts = malloc(sizeof(Vector2) * (size_t)map->n_tris);
		for (int t = 0; t < map->n_tris; t++) {
			vd->verts[t] = circumcenter(map->sites[map->tris[3 * t]].pos,
			                            map->sites[map->tris[3 * t + 1]].pos,
			                            map->sites[map->tris[3 * t + 2]].pos);
		}
	}

	// Dual edges (interior Delaunay edges) + clipped border rays (hull edges).
	for (int t = 0; t < map->n_tris; t++) {
		int *tr = &map->tris[3 * t];
		for (int e = 0; e < 3; e++) {
			int a = tr[e], b = tr[(e + 1) % 3];
			int j = find_partner(map, t, a, b);
			if (j < 0) {
				// Hull edge: ray from the circumcenter outward, clipped to rect.
				Vector2 A = map->sites[a].pos, B = map->sites[b].pos;
				double dx = B.x - A.x, dy = B.y - A.y;
				double len = sqrt(dx * dx + dy * dy);
				if (len < 1e-12) continue;
				// Right of CCW hull edge = outward (triangle interior is left).
				Vector2 dir = { (float)(dy / len), (float)(-dx / len) };
				Vector2 O = vd->verts[t];
				Vector2 F = { (float)(O.x + dir.x * (W + H) * 2),
				              (float)(O.y + dir.y * (W + H) * 2) };
				Vector2 q0, q1;
				if (clip_seg_rect(O, F, W, H, &q0, &q1)) {
					vd->border = realloc(vd->border, sizeof(Vector2) * (size_t)(2 * (vd->n_border + 1)));
					vd->border[2 * vd->n_border] = q0;
					vd->border[2 * vd->n_border + 1] = q1;
					vd->n_border++;
				}
			} else if (j > t) {
				vd->edges = realloc(vd->edges, sizeof(int) * (size_t)(2 * (vd->n_edges + 1)));
				vd->edges[2 * vd->n_edges] = t;
				vd->edges[2 * vd->n_edges + 1] = j;
				vd->n_edges++;
			}
		}
	}
}


void triangulateVoronoiMap(VoronoiMap *map) {
  if (!map) return;

  qsort(map->sites, (size_t)map->n_sites, sizeof(Site), compare_sites);

  // Dedup: equal coordinates would break the merge (zero-length hull edges).
  int w = 0;
  for (int r = 0; r < map->n_sites; r++) {
    if (w > 0 &&
        map->sites[r].pos.x == map->sites[w - 1].pos.x &&
        map->sites[r].pos.y == map->sites[w - 1].pos.y) {
      continue;
    }
    map->sites[w++] = map->sites[r];
  }
  map->n_sites = w;

  // Reset previous results so the call is repeatable.
  for (int i = 0; i < map->n_owned; i++) free(map->owned[i]);
  map->n_owned = 0;
  map->n_tris = 0;
  if (map->n_sites < 2) {
    genVoronoiDiagram(map); // Still valid: 0-1 sites give trivial cells
    return;
  }

  Edge *left_e, *right_e;
  triangulate(map, 0, map->n_sites, &left_e, &right_e);

  // Repair: the D&C seam can strand sub-hull vertices that end up strictly
  // inside the merged hull; insert them, split T-junctions for conformity,
  // then flip back to Delaunay.
  insert_missing_sites(map);
  lawson_flip_pass(map);

  genVoronoiDiagram(map);
}

void relaxVoronoiMap(VoronoiMap *map, int n) {
  if (!map) return;

  for (int i = 0; i < n; i++) {
    for (int s = 0; s < map->n_sites; s++) {

      Vector2 center = (Vector2){ 0.0f, 0.0f };
      float signdArea = 0.0;
      for (int j = 0; j < map->vor.poly_n[s]; j++) {

        int poly_len = map->vor.poly_n[s];
        Vector2 curr_p = map->vor.polys[s][j];
        Vector2 next_p = map->vor.polys[s][(j + 1) % poly_len];

        float factor = (curr_p.x * next_p.y) - (next_p.x * curr_p.y);

        signdArea += factor;
        center.x += (curr_p.x + next_p.x) * factor;
        center.y += (curr_p.y + next_p.y) * factor;

      }

      float area = signdArea / 2.0f;

      if (area == 0.0f) {
        printf("cell %d has zero area!\n", s);
        continue;
      }

      center.x = center.x / (6.0f * area);
      center.y = center.y / (6.0f * area);

      map->sites[s].pos = center;
    }
  }
  
  triangulateVoronoiMap(map);
}

Image genVoronoiImage(VoronoiMap *map) {
  Image image = GenImageColor((int)map->texture_size.x, (int)map->texture_size.y, WHITE);

  VoronoiDiagram *vd = &map->vor;
  if (vd->n_cells == map->n_sites && vd->n_cells > 0) {
    // Cell fills (site colors)
    for (int s = 0; s < vd->n_cells; s++) {
      image_fill_poly(&image, vd->polys[s], vd->poly_n[s], map->sites[s].color);
    }
    // Cell borders (the Voronoi edges)
    Color edge = { 30, 30, 30, 255 };
    for (int s = 0; s < vd->n_cells; s++) {
      for (int k = 0; k < vd->poly_n[s]; k++) {
        Vector2 a = vd->polys[s][k], b = vd->polys[s][(k + 1) % vd->poly_n[s]];
        ImageDrawLine(&image, (int)a.x, (int)a.y, (int)b.x, (int)b.y, edge);
      }
    }

    for (int t = 0; t < map->n_tris; t++) {
      Vector2 a = map->sites[map->tris[3 * t + 0]].pos;
      Vector2 b = map->sites[map->tris[3 * t + 1]].pos;
      Vector2 c = map->sites[map->tris[3 * t + 2]].pos;
      ImageDrawLine(&image, (int)a.x, (int)a.y, (int)b.x, (int)b.y, GRAY);
      ImageDrawLine(&image, (int)b.x, (int)b.y, (int)c.x, (int)c.y, GRAY);
      ImageDrawLine(&image, (int)c.x, (int)c.y, (int)a.x, (int)a.y, GRAY);
    }

  } else {
    // Fallback (no diagram): Delaunay edges
    for (int t = 0; t < map->n_tris; t++) {
      Vector2 a = map->sites[map->tris[3 * t + 0]].pos;
      Vector2 b = map->sites[map->tris[3 * t + 1]].pos;
      Vector2 c = map->sites[map->tris[3 * t + 2]].pos;
      ImageDrawLine(&image, (int)a.x, (int)a.y, (int)b.x, (int)b.y, GRAY);
      ImageDrawLine(&image, (int)b.x, (int)b.y, (int)c.x, (int)c.y, GRAY);
      ImageDrawLine(&image, (int)c.x, (int)c.y, (int)a.x, (int)a.y, GRAY);
    }
  }

  // Sites on top
  for (int i = 0; i < map->n_sites; i++) {
    ImageDrawCircleV(&image, map->sites[i].pos, 3, BLACK);
  }

  return image;
}
