# CGAL provider

This module supplies pinned upstream CGAL headers and BCR-built GMP/MPFR and
Boost inputs. It contains no Autoware or product dependencies. The root selects
this module from `third_party/`; components depend on logical `@cgal` labels.

CGAL 6.1.3's official library archive is used because it provides the flattened
public include tree. Its SHA-256 matches the release asset digest:
https://github.com/CGAL/cgal/releases/tag/v6.1.3

The current integration qualifies `Exact_predicates_inexact_constructions_kernel`
and `Polygon_2_algorithms` on the selected Linux/LLVM profile, as required by the
point-cloud preprocessor. The provider
retains CGAL's GMP/MPFR-enabled exact arithmetic; it does not use host-installed
headers/libraries or silently disable exact predicates. `polygon_test` checks
interior, boundary, exterior, near-collinear orientation and exact rational
arithmetic plus an MPFR square root. Other optional CGAL backends and the entire upstream CGAL test inventory are outside this provider's
qualified surface.
