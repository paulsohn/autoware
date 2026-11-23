// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Gmpq.h>
#include <CGAL/Polygon_2_algorithms.h>
#include <mpfr.h>

#include <array>

int main()
{
  using Kernel = CGAL::Exact_predicates_inexact_constructions_kernel;
  using Point = Kernel::Point_2;
  const std::array polygon{Point(0, 0), Point(2, 0), Point(2, 2), Point(0, 2)};
  if (
    CGAL::bounded_side_2(polygon.begin(), polygon.end(), Point(1, 1), Kernel()) !=
    CGAL::ON_BOUNDED_SIDE)
    return 1;
  if (
    CGAL::bounded_side_2(polygon.begin(), polygon.end(), Point(2, 1), Kernel()) !=
    CGAL::ON_BOUNDARY)
    return 1;
  if (
    CGAL::bounded_side_2(polygon.begin(), polygon.end(), Point(3, 1), Kernel()) !=
    CGAL::ON_UNBOUNDED_SIDE)
    return 1;
  // Nearly collinear doubles exercise the exact fallback rather than only easy predicates.
  if (CGAL::orientation(Point(0, 0), Point(1, 1), Point(2, 2.0000000000000004)) != CGAL::LEFT_TURN)
    return 1;
  if (CGAL::Gmpq(1, 3) + CGAL::Gmpq(2, 3) != CGAL::Gmpq(1)) return 1;
  mpfr_t root;
  mpfr_init2(root, 128);
  mpfr_set_ui(root, 2, MPFR_RNDN);
  mpfr_sqrt(root, root, MPFR_RNDN);
  const double rounded_root = mpfr_get_d(root, MPFR_RNDN);
  mpfr_clear(root);
  if (!(rounded_root > 1.41421356237309 && rounded_root < 1.41421356237310)) return 1;
  return 0;
}
