import numpy as np, json, pathlib, re
from dataclasses import dataclass
import matplotlib.pyplot as plt
import matplotlib.tri as mtri
from matplotlib.collections import PolyCollection
from scipy.spatial import cKDTree
from enum import Enum


ROOT = pathlib.Path(__file__).resolve().parent.parent

# Peak typed into the AxiSim fully-developed inlet BC, converted to base SI.
# solution.npy is base SI (see meta.json) while the GUI shows mm/s, so this is
# 0.0844 mm/s. Bare 0.0844 would be off by 1000x.
INLET_PEAK = 0.0844e-3

class CompareType(Enum):
    POISEUILLE = 1
    OPENFOAM = 2

# AxiSim's name for each field the OpenFOAM export writes. U is one vector file
# carrying two of them; its third component is the wedge direction and means
# nothing here. The concentration is "Conc" and not "C" because OpenFOAM reserves
# C for the cell-centre field -- writeCellCentres overwrites anything called C.
FOAM_FIELDS = {
    "U":    ("Axial Velocity", "Radial Velocity"),
    "p":    "Pressure",
    "T":    "Temperature",
    "Conc": "Concentration",
}

@dataclass
class Solution:
    meta: dict
    sol: np.ndarray
    col: dict[str, int]

    @property
    def live(self):
        return self.c("active") > 0

    def c(self, name):
        return self.sol[:, self.col[name]]

@dataclass
class Cell:
    pts: np.ndarray
    cells: np.ndarray
    verts: np.ndarray

@dataclass
class Poiseuille:
    R: float            # pipe radius
    Umean: float        # realised mean velocity, from the mass flux
    Umax: float         # the peak the analytic profile is built from
    refName: str        # where that Umax came from
    UmaxFlux: float     # 2*Umean -- peak implied by the mass flux
    UmaxDp: float       # peak implied by -dP/dz
    Re: float
    zDev: float         # start of the fully developed region
    dev: np.ndarray     # live AND fully developed cell mask
    u: np.ndarray       # analytic profile at every cell centre (full length)


def cell_map(c, live, values, ax=None, cmap="turbo", label="", clim=None):
    """Paint one value per live cell onto that cell's polygon.

    The polygons are AxiSim's -- they are the only ones either loader builds --
    so everything drawn here is drawn on AxiSim's mesh, whichever code the
    values came from.
    """
    if ax is None:
        _, ax = plt.subplots(figsize=(10,3.2))

    pc = PolyCollection(c.verts[live], array=values,
                    cmap=cmap, edgecolors="none")

    # Left alone, PolyCollection normalises to its own range. Passing clim is
    # what lets two panels be compared by eye rather than only by their labels.
    if clim is not None:
        pc.set_clim(*clim)

    ax.add_collection(pc)
    ax.set_xlim(c.pts[:,0].min(), c.pts[:,0].max())
    ax.set_ylim(c.pts[:,1].min(), c.pts[:,1].max())

    ax.figure.colorbar(pc, ax=ax, label=label)

    return ax

def field_map(s, c, field, mirror=True, ax=None, clim=None):

    return cell_map(c, s.live, s.c(field)[s.live], ax, "turbo", field, clim)

# load solution
def load_solution(folder_name: str) -> Solution:
    d = ROOT / folder_name
    meta = json.loads((d / "meta.json").read_text())
    sol = np.load(d / "solution.npy")
    col = {n : i for i, n in enumerate(meta["columns"])}
    return Solution(meta, sol, col)

# load cells
def load_cells(folder_name: str) -> Cell:
    d = ROOT / folder_name
    pts = np.load(d / "points.npy")
    cells = np.load(d / "cells.npy")
    verts = pts[cells]
    return Cell(pts, cells, verts)

def poiseuille_flow(s: Solution, c: Cell, Umax=None) -> Poiseuille:
    """Analytic Hagen-Poiseuille profile, u = Umax*(1 - r^2/R^2).

    Umax defaults to the solution's own mass flux (2*Umean for a pipe). Pass a
    value to compare against a prescribed peak instead -- e.g. the inlet BC.

    Whichever is chosen, both other routes to the peak are still computed and
    reported as deviations from it. That split is load-bearing: Umax enters as a
    scalar amplitude, so the error is minimised at whichever value best fits the
    field, and a low L2 alone only says the reference happened to land near that
    optimum. The cross-check lines are what distinguish a good solution from a
    well-chosen reference.

    Note u.max() is never a valid choice: it rescales the curve to whatever the
    solver produced, hiding a uniform magnitude error by construction, and lands
    on the inlet column, which has not developed.
    """
    z, r, u, vol = s.c("z"), s.c("r"), s.c("Axial Velocity"), s.c("volume")
    rho, mu = s.meta["fluid"]["rho"], s.meta["fluid"]["mu"]

    # Radius of a straight constant-radius pipe. On any varying-radius or
    # obstructed geometry this is the max radius anywhere rather than the local
    # one, and the whole comparison stops meaning anything.
    R = c.pts[:, 1].max()
    D = 2.0 * R

    # volume = 2*pi*r*dr*dz, so a volume-weighted mean of u over whole z-columns
    # is the annulus-area-weighted mean, i.e. Q/(pi R^2) = Umean. dz cancels, so
    # no grid spacing is needed. Assumes each z-column spans the full radius.
    live = s.live
    Umean = (u[live] * vol[live]).sum() / vol[live].sum()
    Re = rho * Umean * D / mu

    # Entrance length (Durst et al. 2005, valid 0 < Re < 2000). Poiseuille holds
    # only downstream of it; including the entrance region charges its developing
    # profile to the solver as though it were discretisation error.
    zDev = D * (0.619 ** 1.6 + (0.0567 * Re) ** 1.6) ** (1.0 / 1.6)

    dev = live & (z >= zDev)

    if not dev.any():
        print(f"warning: domain shorter than the entrance length ({zDev:.4g} m)"
              f" -- flow never develops, comparing against all live cells")
        dev = live

    # Re-derive over the developed region alone. Mass conservation makes this
    # equal to the whole-domain value; they differ only if some z-column is
    # incomplete, in which case the developed one is the one to trust.
    Umean = (u[dev] * vol[dev]).sum() / vol[dev].sum()
    UmaxFlux = 2.0 * Umean

    # Second, independent route to the same peak. The routes agreeing is what
    # says the reference itself is sound.
    UmaxDp = -s.c("dP/dz")[dev].mean() * R * R / (4.0 * mu)

    if Umax is None:
        Umax, refName = UmaxFlux, "2*Umean"
    else:
        refName = "prescribed"

    return Poiseuille(
        R, Umean, Umax, refName, UmaxFlux, UmaxDp, Re, zDev, dev,
        Umax * (1.0 - (r * r) / (R * R)),
    )

def poiseuille_report(s: Solution, p: Poiseuille):
    """Print the reference, its two cross-checks, and the profile error.

    Separate numbers on purpose. Collapsing them hides bugs: the solver can nail
    the profile shape while the inlet delivers the wrong flow rate.
    """
    res = (s.c("Axial Velocity") - p.u)[p.dev] / p.Umax

    def line(name, val, note):
        print(f"{'Umax  (' + name + ')':<19s}= {val:.6g} m/s   [{note}]")

    print(f"R                 = {p.R:.6g} m")
    print(f"Re                = {p.Re:.4g}")
    print(f"entrance length   = {p.zDev:.6g} m  "
          f"({p.dev.sum()} of {s.live.sum()} live cells kept)")
    print(f"Umean             = {p.Umean:.6g} m/s")
    print()
    line(p.refName, p.Umax, "reference")

    # Skipped when the flux IS the reference, where the line is the identity.
    if p.refName != "2*Umean":
        print(f"{'Umax  (2*Umean)':<19s}= {p.UmaxFlux:.6g} m/s   "
              f"[{100.0 * (p.UmaxFlux / p.Umax - 1.0):+.3f}%  BC fidelity,"
              f" realised vs prescribed]")

    print(f"{'Umax  (-dP/dz)':<19s}= {p.UmaxDp:.6g} m/s   "
          f"[{100.0 * (p.UmaxDp / p.Umax - 1.0):+.3f}%  vs reference]")
    print()
    print(f"L2   error        = {100.0 * np.sqrt((res ** 2).mean()):.4f} % of Umax")
    print(f"Linf error        = {100.0 * np.abs(res).max():.4f} % of Umax")

def field_validation(s, c, compareType, p=None, ax=None):

    if ax is None:
        _, ax = plt.subplots(figsize=(10,3.2))

    if compareType == CompareType.POISEUILLE:
        if p is None:
            p = poiseuille_flow(s, c)

        # Full-length residual, masked exactly once, relative to Umax -- a raw
        # residual of order 1e-6 m/s says nothing on its own.
        res = 100.0 * (s.c("Axial Velocity") - p.u) / p.Umax

        # The whole field is drawn so the entrance region stays visible, but the
        # colour scale comes from the developed region only, otherwise the
        # entrance error saturates it and the developed region reads as flat
        # zero. Symmetric limits on a diverging map so the sign is readable.
        lim = np.abs(res[p.dev]).max()

        pc = PolyCollection(c.verts[s.live], array=res[s.live],
                    cmap="RdBu_r", edgecolors="none")
        pc.set_clim(-lim, lim)

        ax.add_collection(pc)
        ax.axvline(p.zDev, color="k", ls="--", lw=0.8)
        ax.set_xlim(c.pts[:,0].min(), c.pts[:,0].max())
        ax.set_ylim(c.pts[:,1].min(), c.pts[:,1].max())
        ax.figure.colorbar(pc, ax=ax, label="axial velocity error [% of Umax]")

    return ax

@dataclass
class Foam:
    z: np.ndarray                   # cell centres, in AxiSim's (z, r) convention
    r: np.ndarray
    fields: dict[str, np.ndarray]   # keyed by AxiSim field name
    time: str                       # name of the time directory read


def read_foam_internal(path: pathlib.Path):
    """internalField of one ASCII OpenFOAM field file, as (N,) or (N,3).

    Internal field only. Boundary values live on faces rather than cells and have
    no AxiSim cell to pair with.
    """
    text = path.read_text()

    m = re.search(r"internalField\s+nonuniform\s+List<(scalar|vector)>\s*(\d+)?\s*\(", text)

    if m is None:
        # A field that never varied is written as one value rather than a list:
        # `uniform 0` for an untouched scalar, `uniform (0 0 0)` for U.
        u = re.search(r"internalField\s+uniform\s+([^;]+);", text)
        if u is None:
            raise ValueError(f"{path.name}: no internalField found")

        val = np.array([float(x) for x in u.group(1).strip().strip("()").split()])
        return val if val.size > 1 else float(val[0])

    body = text[m.end():]

    if m.group(1) == "scalar":
        return np.fromstring(body[:body.index(")")], sep=" ")

    # Each vector sits in its own parens on its own line, so the list's closing
    # paren is the first one that starts a line.
    rows = re.findall(r"\(([^()]*)\)", body[:body.index("\n)")])
    return np.array([[float(x) for x in row.split()] for row in rows])


def load_foam(case_dir, time=None, rho=1.0) -> Foam:
    """Read an exported OpenFOAM case back in, mapped onto AxiSim's names.

    `rho` converts OpenFOAM's kinematic p back to Pa. It divides out of the whole
    incompressible equation set, so the case itself does not know it -- pass the
    project's, i.e. s.meta["fluid"]["rho"].
    """
    d = pathlib.Path(case_dir).expanduser()

    def numeric(p):
        try:
            float(p.name)
            return True
        except ValueError:
            return False

    if time is None:
        times = sorted((p for p in d.iterdir() if p.is_dir() and numeric(p)),
                       key=lambda p: float(p.name))
        if not times:
            raise FileNotFoundError(f"{d}: no time directories -- has the solver run?")

        # The last one. 0/ is the initial condition, not a result, and is only
        # what is left if the run wrote nothing.
        t = times[-1]
        if t.name == "0":
            print(f"warning: {d} has only 0/ -- comparing against initial conditions")
    else:
        t = d / str(time)

    missing = [n for n in ("Cx", "Cy", "Cz") if not (t / n).exists()]
    if missing:
        raise FileNotFoundError(
            f"{t}: no cell centres ({', '.join(missing)}). Run:\n"
            f"    postProcess -case {d} -func writeCellCentres -latestTime")

    cy, cz = read_foam_internal(t / "Cy"), read_foam_internal(t / "Cz")

    # The wedge straddles the r-z plane, so a cell centre is at z_foam ~ 0 and r
    # is just its distance from the axis. The hypotenuse rather than
    # y/cos(halfAngle) keeps this independent of the wedge angle.
    z, r = read_foam_internal(t / "Cx"), np.hypot(cy, cz)

    fields = {}
    for foam_name, axi in FOAM_FIELDS.items():

        if not (t / foam_name).exists():
            continue

        val = read_foam_internal(t / foam_name)

        if isinstance(axi, tuple):
            fields[axi[0]], fields[axi[1]] = val[:, 0], val[:, 1]
        elif foam_name == "p":
            fields[axi] = val * rho
        else:
            fields[axi] = val

    return Foam(z, r, fields, t.name)


def match_to_foam(s: Solution, f: Foam):
    """Pair every live AxiSim cell with its nearest OpenFOAM cell on (z, r).

    Never pair by index. The two codes number the same mesh differently -- AxiSim
    in block/cellGlobal order, blockMesh in its own -- so a positional diff
    quietly compares unrelated cells and looks plausible doing it.

    The pairing is unambiguous, but the two values in a pair are not sampled at
    quite the same place: OpenFOAM uses the volume-weighted centroid, which on a
    wedge pulls outward in r, while AxiSim uses the r-z midpoint. On the axis cell
    of the reference pipe that is 1.67e-4 against 1.25e-4. Cells are far enough
    apart that nothing mismatches, but near the axis that gap is the same order as
    the error being measured, so read the maps for WHERE the codes disagree rather
    than as a certified error bar.
    """
    live = s.live
    tree = cKDTree(np.column_stack([f.z, f.r]))
    dist, idx = tree.query(np.column_stack([s.c("z")[live], s.c("r")[live]]))
    return live, idx, dist


def foam_residual(s: Solution, f: Foam, live, idx, field):
    """AxiSim minus OpenFOAM for one field, over the live cells, and its scale."""
    a = s.c(field)[live]
    b = f.fields[field][idx]

    if field == "Pressure":
        # Both are defined only up to a constant -- OpenFOAM pins p = 0 at the
        # outlet, AxiSim carries its own datum -- so without removing the offset
        # this measures the choice of datum and nothing else.
        a, b = a - a.mean(), b - b.mean()

    scale = np.abs(a).max()
    return a - b, (scale if scale > 0 else 1.0)


def foam_report(s: Solution, f: Foam, live, idx, dist):
    """Per-field agreement between the two codes."""
    print(f"OpenFOAM time     = {f.time}")
    print(f"cells             = {len(f.z)} OpenFOAM, {live.sum()} live AxiSim")
    print(f"pairing distance  = {dist.max():.3g} m max, {dist.mean():.3g} m mean")

    if len(f.z) != live.sum():
        print(f"warning: cell counts differ -- the two meshes are not the same mesh")

    print()

    shared = [n for n in f.fields if n in s.col]
    if not shared:
        print("no shared fields -- was the case exported from this project?")
        return

    for name in shared:
        res, scale = foam_residual(s, f, live, idx, name)
        print(f"{name:<18s} L2 {100.0 * np.sqrt((res ** 2).mean()) / scale:8.4f} %"
              f"   Linf {100.0 * np.abs(res).max() / scale:8.4f} %"
              f"   [of max |{name}| = {scale:.4g}]")


def foam_validation(s, c, f, live, idx, field="Axial Velocity", ax=None):
    """Map where AxiSim and OpenFOAM disagree, as a percentage of the field."""
    res, scale = foam_residual(s, f, live, idx, field)

    # Symmetric limits on a diverging map so the sign of the disagreement is
    # readable -- which way a cell is wrong is most of the diagnosis.
    err = 100.0 * res / scale
    lim = np.abs(err).max()

    return cell_map(c, live, err, ax, "RdBu_r",
                    f"{field}: AxiSim - OpenFOAM [% of max]", (-lim, lim))


def foam_map(f, c, live, idx, field, ax=None, clim=None, values=None):
    """Draw an OpenFOAM field on its own, not as a difference.

    Foam carries cell centres, not polygons, so the values are painted on the
    AxiSim cells match_to_foam paired them with. That is exact when the two
    meshes are the same mesh, which is the whole point of the export; against
    any other mesh it is a nearest-neighbour resample onto AxiSim's cells and
    throws away whatever the finer of the two resolves. foam_report prints the
    pairing distance and the cell counts -- read those before trusting this.

    `values` overrides the field, for a caller that has already adjusted it.
    """
    if values is None:
        values = f.fields[field][idx]

    return cell_map(c, live, values, ax, "turbo",
                    f"{field}  [OpenFOAM t={f.time}]", clim)


def foam_compare_maps(s, c, f, live, idx, field):
    """One field drawn three ways: AxiSim, OpenFOAM, and the difference.

    The two field maps share one colour scale, deliberately. Allowed to scale
    themselves, both panels normalise to their own range and two fields whose
    magnitudes differ by 20% render as the same picture -- the difference panel
    would be the only sign of it, which defeats plotting the fields at all.
    """
    fig, axes = plt.subplots(3, 1, figsize=(10, 9), sharex=True, sharey=True)

    a, b = s.c(field)[live], f.fields[field][idx]

    if field == "Pressure":
        # The same datum removal foam_residual does. Without it a shared scale
        # renders the gap between the two datums -- OpenFOAM pins p = 0 at the
        # outlet, AxiSim does not -- and the actual disagreement is invisible
        # underneath a constant offset.
        a, b = a - a.mean(), b - b.mean()

    clim = (min(a.min(), b.min()), max(a.max(), b.max()))

    cell_map(c, live, a, axes[0], "turbo", f"{field}  [AxiSim]", clim)
    foam_map(f, c, live, idx, field, axes[1], clim, values=b)
    foam_validation(s, c, f, live, idx, field, ax=axes[2])

    axes[0].set_title(f"{field}:  AxiSim  /  OpenFOAM t={f.time}  /  difference")
    fig.tight_layout()

    return fig


def main():

    compare = CompareType.OPENFOAM

    folder_name = "poiseuille_solution"

    # The exported case. Keep it on the WSL filesystem, not /mnt/c -- OpenFOAM's
    # tiny-file I/O crawls across the 9p mount.
    foam_case = r"\\wsl$\Ubuntu\home\luits\run\pipe-wedge3_case"

    s = load_solution(folder_name)
    c = load_cells(folder_name)

    if compare == CompareType.POISEUILLE:
        p = poiseuille_flow(s, c, Umax=INLET_PEAK)
        poiseuille_report(s, p)
        field_validation(s, c, CompareType.POISEUILLE, p)

    else:
        f = load_foam(foam_case, rho=s.meta["fluid"]["rho"])
        live, idx, dist = match_to_foam(s, f)
        foam_report(s, f, live, idx, dist)

        for name in f.fields:
            if name in s.col:
                foam_compare_maps(s, c, f, live, idx, name)

    field_map(s, c, "Axial Velocity")

    plt.show()


if __name__ == "__main__":
    main()
