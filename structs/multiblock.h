#pragma once
//
// Multi-block structured grid — data model + packer.
//
// This is ON the live path, despite what this comment used to claim: solver/header/
// mesh.h includes it, Mesh owns a MultiBlockMesh member, and toPackedMesh below
// feeds BOTH the solver's device upload (manager/source/memory_manger.cpp) and the
// inspector's FVMesh (solver/source/mesh.cpp). Edit it as solver code.
//
// It defines the geometry + topology of a conformal multi-block structured grid,
// the interface (halo) connectivity a solver consumes, and the assembly that turns
// both into a face-based packed mesh.
//
// Conventions (match the existing solver, e.g. multigrid.cu / linear_solver.cu):
//   - Two logical directions per block: i = radial (r), j = axial (z).
//   - Cell (i,j) local index = i*nz + j          (same as id(i,j,nz)).
//   - Node (I,J) index       = I*(nz+1) + J,  I in [0,nr], J in [0,nz].
//   - Global cell index      = block.globalOffset + local index.
//
// Geometry is stored as general node coordinates (nodePos), so a block can be a
// plain axis-aligned rectangle OR a curvilinear/body-fitted patch. Axis-aligned
// blocks come from makeRectBlock(); curved blocks from makeTransfiniteBlock().
// The interface/halo logic below is purely topological and identical for both.

#include <vector>
#include <cstdint>
#include <cmath>
#include <stdexcept>
#include <string>
#include <set>

#include "core_struct.h"

// Axial (z) / radial (r) node position -- the project's Vec2, under the name the
// multiblock code reads in. An alias rather than a twin struct: they were already
// field-for-field identical, and every boundary between block nodes and the rest of
// the geometry had to hand-repack across the two (see the block-node -> Vec2 lambda
// in mesh.cpp's vertex builder). As the same type, that repacking is unnecessary and
// the Vec2 geometry helpers in math_func.h apply to block nodes directly.
using MBNode = Vec2;

// ---------------------------------------------------------------------------
// 1D spacing zones — the "resolution control" face generator.
// ---------------------------------------------------------------------------
enum class Grading { Uniform, Progression /* , Bump: TODO */ };

struct MeshZone {
    double  length;              // physical extent of this zone
    int     cells;               // number of cells across it
    Grading grading = Grading::Uniform;
    double  coef    = 1.0;       // Progression ratio (>1 grow, <1 shrink along +axis)
};

// Emit monotonically increasing cell-boundary coordinates for a zone list,
// starting at `start`. Returns sum(cells)+1 face coordinates.
inline std::vector<double> generateFaces(const std::vector<MeshZone>& zones, double start) {
    std::vector<double> faces;
    faces.push_back(start);
    double x = start;
    for (const MeshZone& z : zones) {
        const int n = z.cells;
        std::vector<double> d(n, z.length / n);  // uniform default

        if (z.grading == Grading::Progression && n > 1 && z.coef > 0.0 && z.coef != 1.0) {
            // Geometric progression: d_k = d0 * coef^k, sum_k d_k = length.
            const double ratio = z.coef;
            const double d0 = z.length * (ratio - 1.0) / (std::pow(ratio, n) - 1.0);
            for (int k = 0; k < n; k++) d[k] = d0 * std::pow(ratio, k);
        }
        for (int k = 0; k < n; k++) { x += d[k]; faces.push_back(x); }
    }
    return faces;
}

// Which edge of a block, in logical index space.
//   West  = J=0 (min axial)     East  = J=nz-1 (max axial)   -> run along i, count = nr
//   South = i=0 (min radial)    North = i=nr-1 (max radial)  -> run along j, count = nz
enum class Edge { West, East, South, North };

// ---------------------------------------------------------------------------
// Block — one logically-rectangular structured region (nr x nz cells).
// ---------------------------------------------------------------------------
struct Block {
    int id = -1;                       // display label (figure "1".."5")
    int nr = 0;                        // cells in radial (i) direction
    int nz = 0;                        // cells in axial  (j) direction
    std::vector<MBNode>  nodes;        // (nr+1)*(nz+1) node positions
    std::vector<uint8_t> active;       // nr*nz  (1 = fluid, 0 = inactive)
    int globalOffset = 0;              // first global cell index of this block

    // Boundary group id per edge, indexed by (int)Edge {West,East,South,North}.
    // Tags EXTERNAL faces (edges not claimed by an interface); -1 = unset, in
    // which case toPackedMesh falls back to its defaultBoundaryGroup. The id is
    // the same integer your existing BC arrays use (BoundarySegmentGroup.id ->
    // BoundaryFieldDevice per-group slot), so no new BC plumbing is needed.
    int edgeGroup[4] = { -1, -1, -1, -1 };

    void setEdgeGroup(Edge e, int group) { edgeGroup[(int)e] = group; }

    int  nodeIdx(int I, int J) const { return I * (nz + 1) + J; }
    int  cellLocal(int i, int j) const { return i * nz + j; }
    int  cellGlobal(int i, int j) const { return globalOffset + cellLocal(i, j); }
    int  cellCount() const { return nr * nz; }

    const MBNode& node(int I, int J) const { return nodes[nodeIdx(I, J)]; }
    MBNode&       node(int I, int J)       { return nodes[nodeIdx(I, J)]; }
};

// Axis-aligned rectangular block from axial (z) + radial (r) zone specs.
// origin (z0, r0) is the block's minimum corner.
inline Block makeRectBlock(int id, double z0, double r0,
                           const std::vector<MeshZone>& axialZones,    // z (j)
                           const std::vector<MeshZone>& radialZones) { // r (i)
    const std::vector<double> zf = generateFaces(axialZones,  z0);     // nz+1
    const std::vector<double> rf = generateFaces(radialZones, r0);     // nr+1

    Block b;
    b.id = id;
    b.nz = (int)zf.size() - 1;
    b.nr = (int)rf.size() - 1;
    b.nodes.resize((b.nr + 1) * (b.nz + 1));
    for (int I = 0; I <= b.nr; I++)
        for (int J = 0; J <= b.nz; J++)
            b.node(I, J) = MBNode{ zf[J], rf[I] };   // separable tensor product
    b.active.assign(b.cellCount(), 1);
    return b;
}

// Curvilinear block via a transfinite (Coons) blend of its 4 boundary edges.
// Edge arrays list node positions ALONG each edge, consistent at the corners:
//   south (I=0):   J = 0..nz   (length nz+1)     north (I=nr): J = 0..nz
//   west  (J=0):   I = 0..nr   (length nr+1)     east  (J=nz): I = 0..nr
// The 4 boundary polylines may be curved; interior nodes are interpolated.
inline Block makeTransfiniteBlock(int id, int nr, int nz,
                                  const std::vector<MBNode>& south,
                                  const std::vector<MBNode>& north,
                                  const std::vector<MBNode>& west,
                                  const std::vector<MBNode>& east) {
    Block b;
    b.id = id; b.nr = nr; b.nz = nz;
    b.nodes.resize((nr + 1) * (nz + 1));

    const MBNode c00 = south[0];    // (I=0,  J=0)
    const MBNode c0n = south[nz];   // (I=0,  J=nz)
    const MBNode cn0 = north[0];    // (I=nr, J=0)
    const MBNode cnn = north[nz];   // (I=nr, J=nz)

    for (int I = 0; I <= nr; I++) {
        const double u = (double)I / nr;               // radial param
        for (int J = 0; J <= nz; J++) {
            const double v = (double)J / nz;           // axial param
            auto coons = [&](double S, double N, double W, double E,
                             double q00, double q0n, double qn0, double qnn) {
                return (1 - u) * S + u * N + (1 - v) * W + v * E
                     - ((1 - u) * (1 - v) * q00 + (1 - u) * v * q0n
                      +  u       * (1 - v) * qn0 + u       * v * qnn);
            };
            b.node(I, J) = MBNode{
                coons(south[J].z, north[J].z, west[I].z, east[I].z,
                      c00.z, c0n.z, cn0.z, cnn.z),
                coons(south[J].r, north[J].r, west[I].r, east[I].r,
                      c00.r, c0n.r, cn0.r, cnn.r)
            };
        }
    }
    b.active.assign(nr * nz, 1);
    return b;
}

// Straight-edged quad block from 4 corners given in CCW order:
//   A = node(0,0), B = node(0,nz), C = node(nr,nz), D = node(nr,0).
// Bilinear map of the corners (a transfinite block whose 4 edges are straight);
// used for blocks defined by a closed 4-line loop in the sketch.
inline Block makeQuadBlock(int id, const MBNode& A, const MBNode& B,
                           const MBNode& C, const MBNode& D, int nr, int nz) {
    Block b;
    b.id = id; b.nr = nr; b.nz = nz;
    b.nodes.resize((nr + 1) * (nz + 1));
    for (int I = 0; I <= nr; I++) {
        const double t = (double)I / nr;                // radial param
        for (int J = 0; J <= nz; J++) {
            const double s = (double)J / nz;            // axial param
            b.node(I, J) = MBNode{
                (1 - t) * (1 - s) * A.z + (1 - t) * s * B.z + t * s * C.z + t * (1 - s) * D.z,
                (1 - t) * (1 - s) * A.r + (1 - t) * s * B.r + t * s * C.r + t * (1 - s) * D.r
            };
        }
    }
    b.active.assign(nr * nz, 1);
    return b;
}

// Visit every interior/boundary grid-line segment of a block, calling
// emit(a, b) with the two endpoint nodes of each segment: first the lines
// running along j (nr+1 of them), then those running along i. Shared by the
// GL line-vertex builder and the inspector overlay so their traversals can't
// drift. Per-segment (not full-span) so it stays correct for curvilinear
// blocks (makeTransfiniteBlock/makeQuadBlock), not just axis-aligned ones.
template <class Fn>
inline void forEachBlockGridSegment(const Block& b, Fn&& emit) {
    for (int I = 0; I <= b.nr; I++)
        for (int J = 0; J < b.nz; J++)
            emit(b.node(I, J), b.node(I, J + 1));
    // I outer / J inner in this loop too, even though it walks the i-running lines:
    // node index is I*(nz+1)+J, so iterating I innermost would stride a whole row
    // per access and touch a fresh cache line every time, re-walking the node array
    // nz+1 times. This runs per frame from the inspector overlay. Only the order
    // WITHIN this group changes; the set of segments, and the j-group-then-i-group
    // order the callers rely on, are unchanged.
    for (int I = 0; I < b.nr; I++)
        for (int J = 0; J <= b.nz; J++)
            emit(b.node(I, J), b.node(I + 1, J));
}

// ---------------------------------------------------------------------------
// Interfaces (conformal) + halo map.
// ---------------------------------------------------------------------------
inline int edgeCellCount(const Block& b, Edge e) {
    return (e == Edge::West || e == Edge::East) ? b.nr : b.nz;
}

// Local cell indices along an edge, in increasing i (W/E) or increasing j (S/N).
inline std::vector<int> edgeCells(const Block& b, Edge e) {
    std::vector<int> out;
    switch (e) {
        case Edge::West:  for (int i = 0; i < b.nr; i++) out.push_back(b.cellLocal(i, 0));         break;
        case Edge::East:  for (int i = 0; i < b.nr; i++) out.push_back(b.cellLocal(i, b.nz - 1));  break;
        case Edge::South: for (int j = 0; j < b.nz; j++) out.push_back(b.cellLocal(0, j));         break;
        case Edge::North: for (int j = 0; j < b.nz; j++) out.push_back(b.cellLocal(b.nr - 1, j));  break;
    }
    return out;
}

// The two corner node positions of an edge (for orientation auto-detection).
inline void edgeCorners(const Block& b, Edge e, MBNode& start, MBNode& end) {
    switch (e) {
        case Edge::West:  start = b.node(0, 0);      end = b.node(b.nr, 0);      break;
        case Edge::East:  start = b.node(0, b.nz);   end = b.node(b.nr, b.nz);   break;
        case Edge::South: start = b.node(0, 0);      end = b.node(0, b.nz);      break;
        case Edge::North: start = b.node(b.nr, 0);   end = b.node(b.nr, b.nz);   break;
    }
}

// Stable key for a (block index, edge) pair — used to mark interface-claimed
// edges so external-boundary generation and BC tagging can skip them.
inline long long mbEdgeKey(int blockIdx, Edge e) {
    return (long long)blockIdx * 4 + (long long)(int)e;
}

struct Interface {
    int  blockA, blockB;               // indices into MultiBlockMesh::blocks
    Edge edgeA,  edgeB;
    bool reversed = false;             // A's edge order runs opposite to B's
};

// One matched interior-cell pair across an interface. This is the atom of the
// connectivity: feed it to a ghost exchange (copy cellB's value into cellA's
// ghost slot) OR emit it as an internal face (owner=cellA, neighbor=cellB) in
// FVMeshHostPacked. Both coupling strategies consume this same list.
struct InterfacePair {
    int cellA;   // global cell index in blockA (interior cell touching the seam)
    int cellB;   // global cell index in blockB
};

// ---------------------------------------------------------------------------
// Whole mesh: blocks + interfaces + global numbering.
// ---------------------------------------------------------------------------
struct MultiBlockMesh {
    std::vector<Block>     blocks;
    std::vector<Interface> interfaces;
    int totalCells = 0;

    // Lay out contiguous global cell ranges, one per block.
    void assignGlobalNumbering() {
        int off = 0;
        for (Block& b : blocks) { b.globalOffset = off; off += b.cellCount(); }
        totalCells = off;
    }

    // Conformality check: both edges of every interface must have equal cell
    // counts, or the faces don't match 1:1 (that's the non-conformal case).
    void validate() const {
        for (const Interface& itf : interfaces) {
            const int na = edgeCellCount(blocks[itf.blockA], itf.edgeA);
            const int nb = edgeCellCount(blocks[itf.blockB], itf.edgeB);
            if (na != nb)
                throw std::runtime_error(
                    "non-conformal interface between blocks "
                    + std::to_string(blocks[itf.blockA].id) + " and "
                    + std::to_string(blocks[itf.blockB].id) + " ("
                    + std::to_string(na) + " vs " + std::to_string(nb) + " cells)");
        }
    }

    // Build all interface cell-pairs — the halo map. Requires global numbering.
    std::vector<InterfacePair> buildHaloMap() const {
        std::vector<InterfacePair> all;
        for (const Interface& itf : interfaces) {
            const Block& A = blocks[itf.blockA];
            const Block& B = blocks[itf.blockB];
            const std::vector<int> ca = edgeCells(A, itf.edgeA);
            const std::vector<int> cb = edgeCells(B, itf.edgeB);
            if (ca.size() != cb.size())
                throw std::runtime_error("non-conformal interface (run validate() first)");

            const int n = (int)ca.size();
            for (int k = 0; k < n; k++) {
                const int kb = itf.reversed ? (n - 1 - k) : k;
                all.push_back(InterfacePair{ A.globalOffset + ca[k],
                                             B.globalOffset + cb[kb] });
            }
        }
        return all;
    }
};

// Optional: infer `reversed` by comparing shared-edge endpoint coordinates, so
// you don't have to reason about orientation by hand. Aligned if A's edge start
// coincides with B's edge start; reversed if it coincides with B's edge end.
inline bool detectReversed(const Block& A, Edge ea, const Block& B, Edge eb,
                           double tol = 1e-9) {
    MBNode as, ae, bs, be;
    edgeCorners(A, ea, as, ae);
    edgeCorners(B, eb, bs, be);
    auto d2 = [](const MBNode& p, const MBNode& q) {
        const double dz = p.z - q.z, dr = p.r - q.r; return dz * dz + dr * dr;
    };
    (void)tol;
    return d2(as, bs) > d2(as, be);   // A.start closer to B.end => reversed
}

// Cell counts for one structured block. Keyed by sketch-rectangle id in the Mesh
// and edited per block in the Mesh tab's Edit panel (one block per rectangle).
struct BlockResolution {
    int nr = 20;   // radial (i) cells
    int nz = 20;   // axial  (j) cells
};

// Auto-detect conformal interfaces by matching fully-coincident block edges (both
// endpoints coincide within tol). Appends to m.interfaces. Returns false + reason
// on the first matched edge pair whose cell counts differ (non-conformal seam).
inline bool autoDetectInterfaces(MultiBlockMesh& m, std::string& reason,
                                 double tol = 1e-9) {
    const Edge edges[4] = { Edge::West, Edge::East, Edge::South, Edge::North };
    const double tol2 = tol * tol;
    auto coincide = [&](const MBNode& p, const MBNode& q) {
        const double dz = p.z - q.z, dr = p.r - q.r;
        return dz * dz + dr * dr <= tol2;
    };
    for (int a = 0; a < (int)m.blocks.size(); a++) {
        for (int b = a + 1; b < (int)m.blocks.size(); b++) {
            for (Edge ea : edges) {
                for (Edge eb : edges) {
                    MBNode as, ae, bs, be;
                    edgeCorners(m.blocks[a], ea, as, ae);
                    edgeCorners(m.blocks[b], eb, bs, be);
                    const bool aligned  = coincide(as, bs) && coincide(ae, be);
                    const bool reversed = coincide(as, be) && coincide(ae, bs);
                    if (!aligned && !reversed) continue;
                    if (edgeCellCount(m.blocks[a], ea) != edgeCellCount(m.blocks[b], eb)) {
                        reason = "non-conformal seam between blocks "
                               + std::to_string(m.blocks[a].id) + " and "
                               + std::to_string(m.blocks[b].id)
                               + " (shared edge has unequal cell counts)";
                        return false;
                    }
                    m.interfaces.push_back(Interface{ a, b, ea, eb, reversed });
                }
            }
        }
    }
    return true;
}

// Every external edge (one not claimed by an interface) must carry a boundary
// group, or its faces would have no boundary condition. Throws on the first
// untagged external edge. Call after interfaces + edgeGroups are set.
inline void validateBoundaryTags(const MultiBlockMesh& mesh) {
    std::set<long long> claimed;
    for (const Interface& itf : mesh.interfaces) {
        claimed.insert(mbEdgeKey(itf.blockA, itf.edgeA));
        claimed.insert(mbEdgeKey(itf.blockB, itf.edgeB));
    }
    const Edge edges[4] = { Edge::West, Edge::East, Edge::South, Edge::North };
    const char* names[4] = { "West", "East", "South", "North" };
    for (int bi = 0; bi < (int)mesh.blocks.size(); bi++) {
        for (int e = 0; e < 4; e++) {
            if (claimed.count(mbEdgeKey(bi, edges[e]))) continue;   // interface, ok
            if (mesh.blocks[bi].edgeGroup[e] < 0)
                throw std::runtime_error(
                    std::string("untagged external edge ") + names[e] + " on block "
                    + std::to_string(mesh.blocks[bi].id)
                    + " (set edgeGroup or pass a defaultBoundaryGroup)");
        }
    }
}

// ---------------------------------------------------------------------------
// Worked example: the 5-block conformal layout from the figure.
//
//   r=5  +----------1----------+--2--+----------3----------+     (wall, r = R)
//        |  nr=12, wall-graded  | fine|  nr=12, wall-graded  |
//   r=2  +----------+----------+--+--+----------+----------+
//        |    5     |          (notch, external)|    4     |
//   r=0  +----------+                           +----------+
//       z=0        z=4        z=5              z=10
//
// Constraints demonstrated:
//   - Blocks 1,2,3 share the SAME radial distribution (nr=12, same zones) so the
//     vertical interfaces 1-2 and 2-3 are conformal AND coincident.
//   - Block 2 is finer in z (its non-shared direction) — allowed, because its
//     south edge faces the notch (external), not a neighbor.
//   - Interfaces 1-5 and 3-4 match axial counts over their shared z spans.
// ---------------------------------------------------------------------------
inline MultiBlockMesh buildFiveBlockExample() {
    // Shared radial distribution for the top row (r: 2 -> 5), clustered toward
    // the wall at r=5 (coef < 1 shrinks cells along +r). Reused by 1, 2, 3.
    const std::vector<MeshZone> radTop = { { 3.0, 12, Grading::Progression, 0.9 } };
    const std::vector<MeshZone> radBot = { { 2.0,  8, Grading::Uniform,     1.0 } };  // r: 0 -> 2
    const std::vector<MeshZone> axLeft = { { 4.0, 16, Grading::Uniform,     1.0 } };  // z: 0 -> 4
    const std::vector<MeshZone> axMid  = { { 1.0,  6, Grading::Uniform,     1.0 } };  // z: 4 -> 5 (fine)
    const std::vector<MeshZone> axRight= { { 5.0, 20, Grading::Uniform,     1.0 } };  // z: 5 -> 10

    MultiBlockMesh m;
    m.blocks = {
        makeRectBlock(1, 0.0, 2.0, axLeft,  radTop),   // index 0
        makeRectBlock(2, 4.0, 2.0, axMid,   radTop),   // index 1
        makeRectBlock(3, 5.0, 2.0, axRight, radTop),   // index 2
        makeRectBlock(4, 5.0, 0.0, axRight, radBot),   // index 3
        makeRectBlock(5, 0.0, 0.0, axLeft,  radBot),   // index 4
    };

    // All axis-aligned and same orientation here, so reversed = false throughout.
    m.interfaces = {
        { 0, 1, Edge::East,  Edge::West,  false },   // 1 <-> 2  (vertical seam z=4)
        { 1, 2, Edge::East,  Edge::West,  false },   // 2 <-> 3  (vertical seam z=5)
        { 0, 4, Edge::South, Edge::North, false },   // 1 <-> 5  (horizontal seam r=2)
        { 2, 3, Edge::South, Edge::North, false },   // 3 <-> 4  (horizontal seam r=2)
    };

    // Tag external edges with boundary-group ids (indices your BC arrays use):
    //   0 = outer wall (r=5)   1 = inlet (z=0)   2 = outlet (z=10)   3 = axis (r=0)
    // The three edges facing the notch are physical walls, so they reuse group 0.
    enum { WALL = 0, INLET = 1, OUTLET = 2, AXIS = 3 };
    m.blocks[0].setEdgeGroup(Edge::North, WALL);   m.blocks[0].setEdgeGroup(Edge::West,  INLET);
    m.blocks[1].setEdgeGroup(Edge::North, WALL);   m.blocks[1].setEdgeGroup(Edge::South, WALL);   // notch
    m.blocks[2].setEdgeGroup(Edge::North, WALL);   m.blocks[2].setEdgeGroup(Edge::East,  OUTLET);
    m.blocks[3].setEdgeGroup(Edge::East,  OUTLET); m.blocks[3].setEdgeGroup(Edge::South, AXIS);
    m.blocks[3].setEdgeGroup(Edge::West,  WALL);                                                  // notch
    m.blocks[4].setEdgeGroup(Edge::West,  INLET);  m.blocks[4].setEdgeGroup(Edge::South, AXIS);
    m.blocks[4].setEdgeGroup(Edge::East,  WALL);                                                  // notch

    m.assignGlobalNumbering();
    m.validate();               // throws if any interface is non-conformal
    validateBoundaryTags(m);    // throws if any external edge is untagged
    return m;
}

// Rescale a mesh anchored at the origin so its extent fills [0,L] x [0,R]. Lets a
// fixed-coordinate demo (buildFiveBlockExample: 10 x 5) land in the project's real
// domain, so it frames like the old uniform grid. Topology/cell counts unchanged.
inline void fitMultiBlockToBox(MultiBlockMesh& m, double L, double R) {
    double zMax = 0.0, rMax = 0.0;
    for (const Block& b : m.blocks)
        for (const MBNode& n : b.nodes) {
            if (n.z > zMax) zMax = n.z;
            if (n.r > rMax) rMax = n.r;
        }
    const double sz = (zMax > 0.0) ? L / zMax : 1.0;
    const double sr = (rMax > 0.0) ? R / rMax : 1.0;
    for (Block& b : m.blocks)
        for (MBNode& n : b.nodes) { n.z *= sz; n.r *= sr; }
}

// ---------------------------------------------------------------------------
// toPackedMesh — assemble the multi-block grid into a face-based packed mesh
// (Step 3/6). Templated on the packed-mesh type so this header stays
// dependency-free; call it with your FVMeshHostPacked (member names must match:
// nCells, nFaces, nr, nz, face{Owner,Neighbor,NormalZ,NormalR,CenterZ,CenterR,
// Area,BoundaryGroupID}, cell{CenterZ,CenterR,Area2D,Volume,Active,Solid,
// FaceStart,FaceIDs}).
//
// Geometry (axisymmetric r-z), consistent with the existing solver:
//   - cell volume = 2*pi * r_centroid * quad_area        (Pappus revolve)
//   - face area   = 2*pi * r_mid      * edge_length      (Pappus, revolved edge)
//   - face normal = unit, oriented owner -> neighbor (outward for boundary),
//     matching getOutwardNormalForCell()'s owner/neighbor convention.
// Edges not claimed by an interface become boundary faces (neighbor = -1)
// carrying `defaultBoundaryGroup`; mapping those to real BC groups is separate.
// ---------------------------------------------------------------------------

constexpr double MB_PI = 3.14159265358979323846;

// Polygon (quad) cross-section centroid + area via the shoelace formula.
inline void quadCentroidArea(const MBNode& p0, const MBNode& p1,
                             const MBNode& p2, const MBNode& p3,
                             double& cz, double& cr, double& area) {
    const MBNode p[4] = { p0, p1, p2, p3 };
    double A = 0.0, Cz = 0.0, Cr = 0.0;
    for (int k = 0; k < 4; k++) {
        const MBNode& a = p[k];
        const MBNode& b = p[(k + 1) & 3];
        const double cross = a.z * b.r - b.z * a.r;
        A  += cross;
        Cz += (a.z + b.z) * cross;
        Cr += (a.r + b.r) * cross;
    }
    A *= 0.5;
    if (std::fabs(A) < 1e-30) {                         // degenerate: mean fallback
        cz = 0.25 * (p0.z + p1.z + p2.z + p3.z);
        cr = 0.25 * (p0.r + p1.r + p2.r + p3.r);
        area = 0.0;
        return;
    }
    cz = Cz / (6.0 * A);
    cr = Cr / (6.0 * A);
    area = std::fabs(A);
}

// Local cell index + its boundary node-edge for position k along an edge.
inline void edgeFace(const Block& b, Edge e, int k,
                     int& localCell, MBNode& n0, MBNode& n1) {
    switch (e) {
        case Edge::West:  localCell = b.cellLocal(k, 0);         n0 = b.node(k, 0);     n1 = b.node(k + 1, 0);     break;
        case Edge::East:  localCell = b.cellLocal(k, b.nz - 1);  n0 = b.node(k, b.nz);  n1 = b.node(k + 1, b.nz);  break;
        case Edge::South: localCell = b.cellLocal(0, k);         n0 = b.node(0, k);     n1 = b.node(0, k + 1);     break;
        case Edge::North: localCell = b.cellLocal(b.nr - 1, k);  n0 = b.node(b.nr, k);  n1 = b.node(b.nr, k + 1);  break;
    }
}

template <class Packed>
inline void toPackedMesh(const MultiBlockMesh& mesh, Packed& out,
                         int defaultBoundaryGroup = -1) {
    const int nCells = mesh.totalCells;

    out.nr = 0;   // no single logical nr x nz for a multi-block mesh; consumers
    out.nz = 0;   // use the face-based (useFaceCoeffs) solver path.
    out.nCells = nCells;

    out.cellCenterZ.assign(nCells, 0.0);
    out.cellCenterR.assign(nCells, 0.0);
    out.cellArea2D.assign(nCells, 0.0);
    out.cellVolume.assign(nCells, 0.0);
    out.cellActive.assign(nCells, 1);
    out.cellSolid.assign(nCells, 0);

    // ---- 1. Cells: centroid + revolved (Pappus) volume ----
    for (const Block& b : mesh.blocks) {
        for (int i = 0; i < b.nr; i++) {
            for (int j = 0; j < b.nz; j++) {
                const int g = b.cellGlobal(i, j);
                double cz, cr, area;
                quadCentroidArea(b.node(i, j),         b.node(i, j + 1),
                                 b.node(i + 1, j + 1), b.node(i + 1, j),
                                 cz, cr, area);
                const uint8_t act = b.active[b.cellLocal(i, j)];
                out.cellCenterZ[g] = cz;
                out.cellCenterR[g] = cr;
                // Geometry, so it stands whether or not the cell carries flow --
                // matching the structured and triangle cell builders. Only the
                // revolved volume is gated on `act`.
                out.cellArea2D[g]  = area;
                out.cellVolume[g]  = act ? (2.0 * MB_PI * std::fabs(cr) * area) : 0.0;
                out.cellActive[g]  = act;
            }
        }
    }

    // ---- 2. Faces ----
    out.faceOwner.clear();   out.faceNeighbor.clear();
    out.faceNormalZ.clear(); out.faceNormalR.clear();
    out.faceCenterZ.clear(); out.faceCenterR.clear();
    out.faceArea.clear();    out.faceBoundaryGroupID.clear();

    std::vector<std::vector<int>> cellFaces(nCells);

    auto emitFace = [&](int owner, int neighbor,
                        const MBNode& a, const MBNode& b_, int groupID) {
        const int fid = (int)out.faceOwner.size();
        const double dz = b_.z - a.z, dr = b_.r - a.r;
        const double L  = std::sqrt(dz * dz + dr * dr);
        const double fz = 0.5 * (a.z + b_.z), fr = 0.5 * (a.r + b_.r);

        double nz_ = (L > 0.0) ?  dr / L : 0.0;        // perpendicular to the edge
        double nr_ = (L > 0.0) ? -dz / L : 0.0;

        double tz, tr;                                  // orient owner -> neighbor / outward
        if (neighbor >= 0) {
            tz = out.cellCenterZ[neighbor] - out.cellCenterZ[owner];
            tr = out.cellCenterR[neighbor] - out.cellCenterR[owner];
        } else {
            tz = fz - out.cellCenterZ[owner];
            tr = fr - out.cellCenterR[owner];
        }
        if (nz_ * tz + nr_ * tr < 0.0) { nz_ = -nz_; nr_ = -nr_; }

        out.faceOwner.push_back(owner);
        out.faceNeighbor.push_back(neighbor);
        out.faceNormalZ.push_back(nz_);
        out.faceNormalR.push_back(nr_);
        out.faceCenterZ.push_back(fz);
        out.faceCenterR.push_back(fr);
        out.faceArea.push_back(2.0 * MB_PI * std::fabs(fr) * L);
        out.faceBoundaryGroupID.push_back(groupID);

        cellFaces[owner].push_back(fid);
        if (neighbor >= 0) cellFaces[neighbor].push_back(fid);
    };

    // 2a. intra-block internal faces (each interior face exactly once: east + north)
    for (const Block& b : mesh.blocks) {
        for (int i = 0; i < b.nr; i++) {
            for (int j = 0; j < b.nz; j++) {
                const int owner = b.cellGlobal(i, j);
                if (j + 1 < b.nz)                       // east face
                    emitFace(owner, b.cellGlobal(i, j + 1),
                             b.node(i, j + 1), b.node(i + 1, j + 1), -1);
                if (i + 1 < b.nr)                       // north face
                    emitFace(owner, b.cellGlobal(i + 1, j),
                             b.node(i + 1, j), b.node(i + 1, j + 1), -1);
            }
        }
    }

    // 2b. interface faces (cross-block); mark both sides claimed so 2c skips them
    std::set<long long> claimed;
    for (const Interface& itf : mesh.interfaces) {
        claimed.insert(mbEdgeKey(itf.blockA, itf.edgeA));
        claimed.insert(mbEdgeKey(itf.blockB, itf.edgeB));

        const Block& A = mesh.blocks[itf.blockA];
        const Block& B = mesh.blocks[itf.blockB];
        const int count = edgeCellCount(A, itf.edgeA);
        const std::vector<int> cbLocal = edgeCells(B, itf.edgeB);

        for (int k = 0; k < count; k++) {
            int localA; MBNode n0, n1;
            edgeFace(A, itf.edgeA, k, localA, n0, n1);
            const int kb = itf.reversed ? (count - 1 - k) : k;
            emitFace(A.globalOffset + localA,
                     B.globalOffset + cbLocal[kb], n0, n1, -1);
        }
    }

    // 2c. external boundary faces (edges not claimed by any interface)
    const Edge allEdges[4] = { Edge::West, Edge::East, Edge::South, Edge::North };
    for (int bi = 0; bi < (int)mesh.blocks.size(); bi++) {
        const Block& b = mesh.blocks[bi];
        for (Edge e : allEdges) {
            if (claimed.count(mbEdgeKey(bi, e))) continue;
            // Per-edge boundary group; fall back to the caller's default if unset.
            const int gid = (b.edgeGroup[(int)e] >= 0) ? b.edgeGroup[(int)e]
                                                       : defaultBoundaryGroup;
            const int count = edgeCellCount(b, e);
            for (int k = 0; k < count; k++) {
                int localC; MBNode n0, n1;
                edgeFace(b, e, k, localC, n0, n1);
                emitFace(b.globalOffset + localC, -1, n0, n1, gid);
            }
        }
    }

    // ---- 3. CSR cell -> face connectivity ----
    out.cellFaceStart.assign(nCells + 1, 0);
    for (int c = 0; c < nCells; c++)
        out.cellFaceStart[c + 1] = out.cellFaceStart[c] + (int)cellFaces[c].size();
    out.cellFaceIDs.clear();
    out.cellFaceIDs.reserve((size_t)out.cellFaceStart[nCells]);
    for (int c = 0; c < nCells; c++)
        for (int fid : cellFaces[c]) out.cellFaceIDs.push_back(fid);

    out.nFaces = (int)out.faceOwner.size();
}
