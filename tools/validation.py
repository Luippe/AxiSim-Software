import numpy as np, json, pathlib, re
from dataclasses import dataclass
import matplotlib.pyplot as plt
import matplotlib.tri as mtri
from matplotlib.collections import PolyCollection
from scipy.spatial import cKDTree
from enum import Enum


HERE = pathlib.Path(__file__).resolve().parent          # tools/
ROOT = HERE.parent                                      # repo root

# Peak typed into the AxiSim fully-developed inlet BC, converted to base SI.
# solution.npy is base SI (see meta.json) while the GUI shows mm/s, so this is
# 0.0844 mm/s. Bare 0.0844 would be off by 1000x.
INLET_PEAK = 0.0844e-3

class CompareType(Enum):
    POISEUILLE = 1
    OPENFOAM = 2
    EXPERIMENT = 3

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


def cell_map(c, live, values, ax=None, cmap="turbo", label="", clim=None,
             aspect=None):
    """Paint one value per live cell onto that cell's polygon.

    The polygons are AxiSim's -- they are the only ones either loader builds --
    so everything drawn here is drawn on AxiSim's mesh, whichever code the
    values came from.

    `aspect` controls the r:z scaling:
      None      matplotlib's "auto" -- the data is stretched to fill the axes
                box, so the radial exaggeration is whatever figsize happens to
                be (14.9x on the FDA nozzle) and it changes on a window resize.
      "equal"   true scale, 1 m of r drawn as 1 m of z. Honest, but the nozzle
                is 280 x 6 mm, so the whole domain is a 47:1 hairline -- pair it
                with an xlim around the region of interest.
      a number  explicit exaggeration: 1 is "equal", 5 draws r 5x tall. Fixed at
                any window size, which is the point -- the distortion becomes a
                number in the code rather than an accident of the figure shape.
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

    # After the limits, and left alone when None so the existing multi-panel
    # figures keep filling their boxes. adjustable stays at the default "box":
    # the axes shrink to satisfy the ratio. The other option, "datalim", holds
    # the box and widens the limits instead, which here just pads the domain
    # with empty space rather than showing more of it.
    if aspect is not None:
        ax.set_aspect(aspect)

    ax.figure.colorbar(pc, ax=ax, label=label)

    return ax

def field_map(s, c, field, mirror=True, ax=None, clim=None, aspect=None):

    return cell_map(c, s.live, s.c(field)[s.live], ax, "turbo", field, clim,
                    aspect)

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

    axes[0].set_title(f"{field}:  AxiSim (top)   OpenFOAM t={f.time}  (middle)   difference (bottom)")
    fig.tight_layout()

    return fig


# ======================================================================
# -------------------FDA NOZZLE PIV EXPERIMENT--------------------------
# ======================================================================
# Interlaboratory PIV for the FDA benchmark nozzle, sudden-expansion orientation.
# Unzip SE_exp_0500.zip from
#   https://github.com/OSEL-DAM/CFD-and-Blood-Damage-Benchmarks  (Nozzle/Data)
# into `experiment/` NEXT TO THIS SCRIPT -- it sits under tools/, not at the repo
# root, because tools/ is gitignored and the measurements are not ours to vendor.
# The old nciphub.org home of this dataset is dead -- the domain no longer resolves.
#
# Unlike the OpenFOAM comparison this is not a code-to-code check: these are
# measurements, with a real uncertainty, and the five files are five independent
# lab datasets rather than five repeats. So nothing here reduces them to one
# curve -- the spread between them IS the tolerance the solver has to land in.
PIV_DIR = HERE / "experiment"

# The nozzle is run in both directions, and both orientations unzip into the same
# folder as the same five lab codes measured on a different geometry. So the
# folder does not say which case is being compared and an unfiltered read merges
# two geometries into one band -- silently, since it fails no check: the codes
# look like ten labs and the stations like a union.
#
# Filtered on the header rather than the filename because the two datasets do not
# share a naming convention ("PIV_Sudden_Expansion_500_243" against
# "PIV_conical_diffuser_500_243"). Set to None to read whatever is there.
PIV_ORIENTATION = "Sudden Expansion"

# Profile block name (the part between "plot-profile-" and "-at-z") -> the AxiSim
# field it should be compared against. Reynolds stress is deliberately absent: at
# Re = 500 the flow is laminar and the block is ~0 everywhere, present only so the
# file layout matches the higher-Re cases.
PIV_FIELDS = {
    "axial-velocity":  "Axial Velocity",
    "radial-velocity": "Radial Velocity",
    "shear-stress":    "Shear Stress",
}

# Odd in r: these flip sign when the profile is folded about the axis. The files
# store a full diameter, so the r < 0 branch of a radial velocity is the negative
# of the same physical value. Axial velocity is even, and the shear-stress block
# is a magnitude (verified: no negative entries anywhere in it), so neither flips.
PIV_ODD_IN_R = {"radial-velocity"}


@dataclass
class PivLab:
    code: str                       # the trailing number in the filename
    header: dict[str, str]
    profiles: dict                  # (quantity, z) -> (n, 2) array of (r, value)
    axial: dict                     # block name  -> (n, 2) array of (z, value)

    @property
    def rho(self):  return float(self.header["fluid-density"])
    @property
    def mu(self):   return float(self.header["fluid-viscosity"])
    @property
    def flow(self): return float(self.header["fluid-volumetric-flow-rate"])
    @property
    def orientation(self): return self.header.get("dataset-orientation", "")


@dataclass
class Piv:
    labs: list[PivLab]
    stations: list[float]           # every z any lab has an axial-velocity profile at


@dataclass
class PivBand:
    r: np.ndarray                   # common radius grid, folded so r >= 0
    lo: np.ndarray                  # envelope over every lab and both branches
    hi: np.ndarray
    mean: np.ndarray
    codes: list[str]                # labs that contributed
    ncurves: int


def read_piv_file(path: pathlib.Path) -> PivLab:
    """One lab's PIV file: `key value` header lines, then self-describing blocks.

    A block is a name line, a count line, then that many 2-column rows. The count
    is authoritative and must be read -- the files do NOT agree on it. Codes 243,
    468 and 763 use 100 points per profile, 297 uses 38-121 and 999 uses 37-111.
    """
    header, profiles, axial = {}, {}, {}
    lines = path.read_text().splitlines()

    i = 0
    while i < len(lines):
        parts = lines[i].split()
        i += 1

        if not parts:
            continue

        name = parts[0]

        if not name.startswith("plot-"):
            header[name] = " ".join(parts[1:]).strip('"')
            continue

        n = int(lines[i])
        i += 1

        rows = np.array([[float(x) for x in lines[i + k].split()] for k in range(n)])
        i += n

        # "plot-profile-<quantity>-at-z <z> 0" -- radial profile at a station.
        # Everything else is a distribution along z and keeps its full name.
        if name.startswith("plot-profile-") and name.endswith("-at-z"):
            quantity = name[len("plot-profile-"):-len("-at-z")]
            profiles[(quantity, round(float(parts[1]), 6))] = rows
        else:
            axial[name] = rows

    return PivLab(path.stem.split("_")[-1], header, profiles, axial)


def load_piv(folder=PIV_DIR, orientation=PIV_ORIENTATION) -> Piv:
    """Every lab file of one orientation, plus the union of their stations.

    Union, not intersection: code 468 is missing z = +0.016, +0.024 and +0.080
    (42 blocks against the others' 54), and dropping three stations everywhere to
    accommodate it would throw away data the other four labs did measure.

    Union across ORIENTATIONS is the one thing that would not be data -- see
    PIV_ORIENTATION. Every file is read before filtering; they are ~150 kB each
    and the header is the only thing that identifies them.
    """
    d = pathlib.Path(folder)

    paths = sorted(d.glob("*.txt"))
    if not paths:
        raise FileNotFoundError(
            f"{d}: no PIV .txt files -- unzip SE_exp_0500.zip here")

    labs = [read_piv_file(p) for p in paths]

    if orientation is not None:
        keep = [lab for lab in labs if lab.orientation == orientation]

        if not keep:
            found = sorted({lab.orientation for lab in labs})
            raise FileNotFoundError(
                f"{d}: no {orientation!r} files among {len(labs)} read"
                f" -- found {found}")

        labs = keep

    stations = sorted({z for lab in labs for (q, z) in lab.profiles
                       if q == "axial-velocity"})

    return Piv(labs, stations)


def trim_edge_zeros(r, v):
    """Drop the leading and trailing runs of exact zeros.

    The PIV window is wider than the tube, so every profile is padded out to the
    window with exact 0.0 where there is no fluid. Those are not measurements, and
    an error metric taken over them is dominated by invented wall data -- they are
    also, usefully, what marks where the wall is.

    Only the runs at each END go. An interior exact zero is kept: u genuinely
    passes through zero inside the recirculation zone behind the step.
    """
    nz = np.flatnonzero(v != 0.0)

    if nz.size == 0:
        return r[:0], v[:0]

    return r[nz[0]:nz[-1] + 1], v[nz[0]:nz[-1] + 1]


def piv_curves(lab: PivLab, quantity: str, z: float, recenter=False):
    """One lab's profile at z, as up to two (|r|, value) curves sorted by |r|.

    The branches are returned separately rather than averaged. The two sides of
    the same measurement disagree by more than the fit error in places, and that
    asymmetry is part of the experimental uncertainty -- merging it here would
    make the band look tighter than the experiment was.

    `recenter` shifts r so the wetted span is symmetric about the axis. It is off
    by default because the misalignment is small and silently moving measured data
    is worse than reporting it: across all five files and twelve stations the
    wetted midpoint sits within 0.1 mm of r = 0, under 2% of the inlet radius.
    Code 999 is already exactly symmetric.
    """
    rows = lab.profiles.get((quantity, round(z, 6)))

    if rows is None:
        return []

    r, v = rows[:, 0], rows[:, 1]

    # Code 297 stores r descending; everything below assumes ascending.
    if r[0] > r[-1]:
        r, v = r[::-1], v[::-1]

    r, v = trim_edge_zeros(r, v)

    if r.size == 0:
        return []

    if recenter:
        r = r - 0.5 * (r[0] + r[-1])

    curves = []

    for side in (-1.0, +1.0):
        m = (side * r) > 0

        # A branch needs two points to interpolate on. Inside the throat, where a
        # station spans only +/-2 mm, one side can fall below that.
        if m.sum() < 2:
            continue

        rr, vv = np.abs(r[m]), v[m]

        if side < 0 and quantity in PIV_ODD_IN_R:
            vv = -vv

        o = np.argsort(rr)
        curves.append((rr[o], vv[o]))

    return curves


def piv_band(piv: Piv, quantity: str, z: float, n=80, recenter=False):
    """Envelope of every lab and both branches at one station, on a common grid.

    The labs sample r on different grids (spacing 0.107-0.189 mm) and disagree on
    where the wall is by about 0.2 mm, so the grid spans only the radii EVERY
    curve covers. That trims to the innermost wall rather than extrapolating a lab
    past its own last sample -- extrapolated ends would widen the band exactly
    where it is being read most closely.
    """
    curves, codes = [], []

    for lab in piv.labs:
        cs = piv_curves(lab, quantity, z, recenter)
        curves += cs

        if cs:
            codes.append(lab.code)

    if not curves:
        return None

    r = np.linspace(max(c[0][0] for c in curves),
                    min(c[0][-1] for c in curves), n)

    vals = np.array([np.interp(r, cr, cv) for cr, cv in curves])

    return PivBand(r, vals.min(0), vals.max(0), vals.mean(0), codes, len(curves))


def axisim_field(s: Solution, name: str):
    """One AxiSim field by name, including the ones it does not store directly.

    The PIV files report a viscous shear-stress magnitude, which has no solution
    column -- but the velocity gradients do, so tau_rz is reconstructed here
    rather than left uncomparable. Magnitude, to match the sign convention of the
    measured block.
    """
    if name in s.col:
        return s.c(name)

    if name == "Shear Stress":
        return np.abs(s.meta["fluid"]["mu"] * (s.c("dU/dr") + s.c("dV/dz")))

    raise KeyError(f"{name}: not a solution column and not derived here")


def profile_sampler(s: Solution):
    """Build the triangulation once, hand back a (field, z, r) -> values sampler.

    Linear interpolation, not nearest-neighbour: nearest would quantise the
    profile to the cell size, which near the wall is the same order as the
    difference being measured.

    The Delaunay triangulation spans the concave corner upstream of the step --
    it knows nothing about the solid there -- so a query inside the solid would be
    silently interpolated across it. Every caller takes its radii from a PIV
    profile, which trim_edge_zeros has already masked to the fluid, so the query
    points stay inside. Anything sampled from elsewhere must respect that.
    """
    live = s.live
    tri = mtri.Triangulation(s.c("z")[live], s.c("r")[live])
    cache = {}

    def sample(field, z, r):
        if field not in cache:
            cache[field] = mtri.LinearTriInterpolator(tri, axisim_field(s, field)[live])

        # Outside the hull comes back masked; NaN keeps it out of the metrics
        # instead of quietly reading as zero.
        return np.ma.filled(cache[field](np.full_like(r, z), r), np.nan)

    return sample


def axisim_flow_rate(s: Solution, z=None, sample=None, n=400):
    """Volumetric flow rate through one cross-section, by integrating 2*pi*r*u dr.

    NOT the volume-weighted mean poiseuille_flow uses. That route goes through the
    identity Umean = Q/(pi R^2), which holds only on a constant-radius pipe; this
    geometry has a throat one third of the inlet diameter, and the whole-domain
    volume-weighted mean there is a blend of throat and pipe that is not the flow
    rate anywhere. Integrating a single station is exact on any radius.

    Defaults to the inlet plane, which is what makes this a check on the BC.
    Slightly under-reads: the outermost sample is a cell centre, half a cell
    inside the wall, so a thin annulus is missed -- u is near zero there, so the
    truncation is second order (~0.1% on a 44-cell radius).
    """
    live = s.live
    zc, rc = s.c("z")[live], s.c("r")[live]

    half = (zc.max() - zc.min()) / 200.0

    if z is None:
        # One slab in, NOT on the first cell centres themselves. Those centres
        # are the upstream edge of the triangulation, so a query sitting exactly
        # on them lands on the hull and every sample comes back masked -- and an
        # integral over nothing is 0.0 rather than an error, so this reported a
        # flow rate of zero, and piv_case_check a Reynolds number of zero, on a
        # perfectly good solution.
        z = zc.min() + half

    slab = np.abs(zc - z) <= half

    if not slab.any():
        raise ValueError(f"no live cells near z = {z}")

    r = np.linspace(0.0, rc[slab].max(), n)

    if sample is None:
        sample = profile_sampler(s)

    u = sample("Axial Velocity", z, r)
    ok = np.isfinite(u)

    # Partial coverage under-reads by whatever fraction of the radius fell
    # outside the hull, which looks exactly like a mis-scaled inlet -- the one
    # thing this function exists to detect. Refusing beats a plausible wrong
    # number. The default plane clears this easily (397 of 400 on the FDA case);
    # what it catches is a z on or past either end of the mesh.
    if ok.sum() < 0.9 * n:
        raise ValueError(
            f"only {ok.sum()} of {n} samples at z = {z:.6g} are inside the mesh"
            f" -- the integral would under-read; pass an interior z")

    return 2.0 * np.pi * np.trapezoid(u[ok] * r[ok], r[ok])


def piv_case_check(s: Solution, piv: Piv, sample=None):
    """Compare the solved case against the conditions written in the PIV header.

    The header carries the density, viscosity and flow rate the experiment was run
    at, so a mis-set fluid or a mis-scaled inlet shows up here rather than as a
    mysterious velocity error twelve panels later.
    """
    lab = piv.labs[0]

    rho, mu = s.meta["fluid"]["rho"], s.meta["fluid"]["mu"]

    print(f"labs              = {', '.join(l.code for l in piv.labs)}"
          f"  ({len(piv.stations)} stations)")
    print(f"rho               = {rho:.6g} AxiSim vs {lab.rho:.6g} experiment"
          f"   [{100.0 * (rho / lab.rho - 1.0):+.3f}%]")
    print(f"mu                = {mu:.6g} AxiSim vs {lab.mu:.6g} experiment"
          f"   [{100.0 * (mu / lab.mu - 1.0):+.3f}%]")

    Q = axisim_flow_rate(s, sample=sample)

    print(f"flow rate         = {Q:.6g} AxiSim vs {lab.flow:.6g} experiment"
          f"   [{100.0 * (Q / lab.flow - 1.0):+.3f}%]   (inlet plane)")

    # Re on the throat diameter, the benchmark's own definition -- 500 is the
    # laminar case and the only one AxiSim can claim, having no turbulence model.
    dThroat = 0.004
    print(f"Re (throat)       = {4.0 * rho * Q / (np.pi * dThroat * mu):.1f}"
          f"   [benchmark: 500]")

    if any(l.rho != lab.rho or l.mu != lab.mu for l in piv.labs):
        print("warning: the lab files do not agree on the fluid properties")


def piv_report(s: Solution, piv: Piv, quantity="axial-velocity", recenter=False,
               sample=None):
    """Per-station agreement with the measurements.

    Two numbers per station, deliberately. The L2 is against the mean of the labs
    and says how far off the solver is; "in band" is the fraction of the profile
    that lands inside the interlaboratory envelope and says whether that distance
    is even resolvable by this experiment. A 4% L2 that is 100% in band is a pass;
    the same 4% entirely outside the band is not.
    """
    field = PIV_FIELDS[quantity]

    if sample is None:
        sample = profile_sampler(s)

    print()
    print(f"{quantity}  ->  AxiSim '{field}'")
    print(f"{'z (mm)':>8s} {'curves':>7s} {'scale':>11s} "
          f"{'L2':>9s} {'Linf':>9s} {'in band':>9s}")

    tot_res, tot_scale2, tot_in, tot_n = [], [], 0, 0

    for z in piv.stations:
        band = piv_band(piv, quantity, z, recenter=recenter)

        if band is None:
            continue

        a = sample(field, z, band.r)
        ok = np.isfinite(a)

        if not ok.any():
            print(f"{z * 1e3:8.1f} {band.ncurves:7d} "
                  f"{'--':>11s} {'--':>9s} {'--':>9s} {'--':>9s}"
                  "   (station outside the mesh)")
            continue

        res = a[ok] - band.mean[ok]

        # Per-station scale, so a station in the slow recirculation zone is not
        # judged against the jet's magnitude.
        scale = np.abs(band.mean[ok]).max()
        scale = scale if scale > 0 else 1.0

        inb = ((a[ok] >= band.lo[ok]) & (a[ok] <= band.hi[ok])).sum()

        print(f"{z * 1e3:8.1f} {band.ncurves:7d} {scale:11.4g} "
              f"{100.0 * np.sqrt((res ** 2).mean()) / scale:8.3f}% "
              f"{100.0 * np.abs(res).max() / scale:8.3f}% "
              f"{100.0 * inb / ok.sum():8.1f}%")

        tot_res.append(res)
        tot_scale2.append(np.full(res.shape, scale))
        tot_in += inb
        tot_n += ok.sum()

    if not tot_res:
        print("no station could be sampled -- is this the right solution folder?")
        return

    # Pooled over stations, each normalised by its own scale first so the jet does
    # not drown out the recirculation zone.
    rel = np.concatenate(tot_res) / np.concatenate(tot_scale2)

    print(f"{'all':>8s} {'':>7s} {'':>11s} "
          f"{100.0 * np.sqrt((rel ** 2).mean()):8.3f}% "
          f"{100.0 * np.abs(rel).max():8.3f}% "
          f"{100.0 * tot_in / tot_n:8.1f}%")


def piv_validation(s: Solution, piv: Piv, quantity="axial-velocity",
                   recenter=False, ncol=4, sample=None):
    """One panel per station: interlab envelope, its mean, and AxiSim over it."""
    field = PIV_FIELDS[quantity]

    if sample is None:
        sample = profile_sampler(s)

    stations = [z for z in piv.stations
                if piv_band(piv, quantity, z, recenter=recenter) is not None]

    nrow = int(np.ceil(len(stations) / ncol))
    fig, axes = plt.subplots(nrow, ncol, figsize=(3.4 * ncol, 2.8 * nrow),
                             squeeze=False)

    for ax, z in zip(axes.flat, stations):
        band = piv_band(piv, quantity, z, recenter=recenter)
        a = sample(field, z, band.r)

        # fill_betweenx, not fill_between: the profile is plotted with the value
        # on x and r up the y axis, so the band spans x at fixed y.
        ax.fill_betweenx(band.r * 1e3, band.lo, band.hi,
                         color="0.75", label="labs (envelope)")
        ax.plot(band.mean, band.r * 1e3, color="0.35", lw=1.0, ls="--",
                label="lab mean")
        ax.plot(a, band.r * 1e3, color="tab:red", lw=1.4, label="AxiSim")

        ax.set_title(f"z = {z * 1e3:+.0f} mm  ({band.ncurves} curves)", fontsize=9)
        ax.tick_params(labelsize=8)
        ax.grid(alpha=0.25)

    for ax in axes.flat[len(stations):]:
        ax.set_visible(False)

    for ax in axes[-1]:
        if ax.get_visible():
            ax.set_xlabel(field, fontsize=8)

    for row in axes:
        row[0].set_ylabel("r (mm)", fontsize=8)

    axes[0][0].legend(fontsize=7, loc="best")
    fig.suptitle(f"FDA nozzle, sudden expansion, Re_throat = 500 -- {field}")
    fig.tight_layout()

    return fig


def main():

    compare = CompareType.EXPERIMENT
    # compare = CompareType.OPENFOAM

    folder_name = "SE_sim_0500_solution"

    # The exported case. Keep it on the WSL filesystem, not /mnt/c -- OpenFOAM's
    # tiny-file I/O crawls across the 9p mount.
    foam_case = r"\\wsl$\Ubuntu\home\luits\run\SE_sim_0500_case"

    s = load_solution(folder_name)
    c = load_cells(folder_name)

    if compare == CompareType.POISEUILLE:
        p = poiseuille_flow(s, c, Umax=INLET_PEAK)
        poiseuille_report(s, p)
        field_validation(s, c, CompareType.POISEUILLE, p)

    elif compare == CompareType.EXPERIMENT:
        piv = load_piv()

        # One triangulation for all seven passes below -- rebuilding it per call
        # is a Delaunay over every live cell each time.
        sample = profile_sampler(s)

        piv_case_check(s, piv, sample)

        # Axial velocity first: it is the quantity the labs agree on best, so a
        # disagreement there is the solver's. Shear stress is a derivative of the
        # measured field on one side and of the solution on the other, and is the
        # loosest of the three -- read it last.
        for quantity in ("axial-velocity", "radial-velocity", "shear-stress"):
            piv_report(s, piv, quantity, sample=sample)
            piv_validation(s, piv, quantity, sample=sample)

    else:
        f = load_foam(foam_case, rho=s.meta["fluid"]["rho"])
        live, idx, dist = match_to_foam(s, f)
        foam_report(s, f, live, idx, dist)

        for name in f.fields:
            if name in s.col:
                foam_compare_maps(s, c, f, live, idx, name)

    field_map(s, c, "Axial Velocity", aspect=5)

    plt.show()


if __name__ == "__main__":
    main()
