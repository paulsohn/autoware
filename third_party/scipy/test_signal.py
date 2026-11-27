# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Exercise numerical native extensions used by signal analysis and solvers."""
import unittest
import numpy as np
from scipy import linalg, signal


class ScientificRuntime(unittest.TestCase):
    def test_linear_algebra_and_signal_filter(self):
        np.testing.assert_allclose(linalg.solve([[3.0, 1.0], [1.0, 2.0]], [9.0, 8.0]), [2.0, 3.0])
        samples = np.arange(200) / 100.0
        clean = np.sin(2 * np.pi * samples)
        noisy = clean + 0.2 * np.sin(2 * np.pi * 30 * samples)
        b, a = signal.butter(3, 0.1)
        filtered = signal.filtfilt(b, a, noisy)
        self.assertLess(np.mean((filtered - clean) ** 2), np.mean((noisy - clean) ** 2))


if __name__ == '__main__':
    unittest.main()
