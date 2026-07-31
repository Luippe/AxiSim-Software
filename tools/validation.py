import numpy as np, json, pathlib
from dataclasses import dataclass
import matplotlib.pyplot as plt
import matplotlib.tri as mtri
from matplotlib.collections import PolyCollection
from enum import Enum


ROOT = pathlib.Path(__file__).resolve().parent.parent

# Peak typed into the AxiSim fully-developed inlet BC, converted to base SI.
# solution.npy is base SI (see meta.json) while the GUI shows mm/s, so this is
# 0.0844 mm/s. Bare 0.0844 would be off by 1000x.
INLET_PEAK = 0.0844e-3

class CompareType(Enum):
    POISEUILLE = 1

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


def field_map(s, c, field, mirror=True, ax=None):

    if ax is None:
        _, ax = plt.subplots(figsize=(10,3.2))

    live = s.live
    pc = PolyCollection(c.verts[live], array=s.c(field)[live],
                    cmap="turbo", edgecolors="none")

    ax.add_collection(pc)
    ax.set_xlim(c.pts[:,0].min(), c.pts[:,0].max())
    ax.set_ylim(c.pts[:,1].min(), c.pts[:,1].max())

    ax.figure.colorbar(pc, ax=ax, label=field)

    return ax

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

def main():

    folder_name = "poiseuille_solution"
    s = load_solution(folder_name)
    c = load_cells(folder_name)

    p = poiseuille_flow(s, c, Umax=INLET_PEAK)

    poiseuille_report(s, p)

    field_map(s, c, "Axial Velocity")

    field_validation(s, c, CompareType.POISEUILLE, p)

    plt.show()


if __name__ == "__main__":
    main()
