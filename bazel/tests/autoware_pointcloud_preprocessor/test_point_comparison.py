# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Check the upstream fixture ordering without weakening its assertions."""
import os
import runpy

import numpy as np
import pytest

sort_points = runpy.run_path(os.environ["AUTOWARE_TEST_SOURCE"])["sort_points"]


def test_accepts_permutation_and_preserves_multiplicity():
    expected = np.array([[0, 0, 0], [1, 1, 1], [1, 1, 1]], dtype=np.float32)
    np.testing.assert_allclose(sort_points(expected[[1, 0, 2]]), sort_points(expected))
    with pytest.raises(AssertionError):
        np.testing.assert_allclose(sort_points(expected[[0, 0, 1]]), sort_points(expected))


def test_rejects_missing_and_changed_points_with_original_tolerance():
    expected = np.array([[0, 0, 0], [1, 1, 1]], dtype=np.float32)
    with pytest.raises(AssertionError):
        np.testing.assert_allclose(sort_points(expected[:1]), sort_points(expected), atol=1e-3)
    changed = expected.copy()
    changed[0, 2] = 0.1
    with pytest.raises(AssertionError):
        np.testing.assert_allclose(sort_points(changed), sort_points(expected), atol=1e-3)
    changed[0, 2] = 0.0005
    np.testing.assert_allclose(sort_points(changed), sort_points(expected), atol=1e-3)


def test_empty_clouds_keep_shape_validation():
    empty = np.empty((0, 3), dtype=np.float32)
    np.testing.assert_allclose(sort_points(empty), sort_points(empty))
    with pytest.raises(AssertionError):
        np.testing.assert_allclose(sort_points(empty), sort_points(np.zeros((1, 3))))
