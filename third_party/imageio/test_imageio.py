# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0

import io
import unittest

import imageio.v2 as imageio
import numpy as np


class GifRoundtrip(unittest.TestCase):
    def test_frames_preserve_pixels(self):
        frames = [np.zeros((4, 4, 3), dtype=np.uint8), np.full((4, 4, 3), 255, dtype=np.uint8)]
        stream = io.BytesIO()
        imageio.mimsave(stream, frames, format='GIF', duration=100, loop=0)
        restored = imageio.mimread(stream.getvalue(), format='GIF')
        self.assertEqual(len(restored), 2)
        for expected, actual in zip(frames, restored):
            np.testing.assert_array_equal(actual, expected)


if __name__ == '__main__':
    unittest.main()
