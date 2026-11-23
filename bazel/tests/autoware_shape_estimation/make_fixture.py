# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Synthetic shape protocol: point means and class-selected fixed box dimensions.

ONNX IR 8 / opset 13; no trained model, external downloads or generator dependency.
The native GPU gate parses and executes both batch sizes through the node.
"""
from pathlib import Path
import struct


def varint(value):
    result = bytearray()
    while value > 127:
        result.append((value & 127) | 128)
        value >>= 7
    return bytes(result) + bytes([value])


def field(number, value):
    if isinstance(value, int):
        return varint(number << 3) + varint(value)
    if isinstance(value, str):
        value = value.encode()
    return varint((number << 3) | 2) + varint(len(value)) + value


def tensor(name, shape):
    dims = b"".join(field(1, field(1, dim)) for dim in shape)
    return field(1, name) + field(2, field(1, field(1, 1) + field(2, dims)))


axes = field(1, "axes") + field(8, 2) + field(20, 7)
keepdims = field(1, "keepdims") + field(3, 0) + field(20, 2)
mean = (field(1, "pc") + field(2, "stage1_center") + field(4, "ReduceMean")
        + field(5, axes) + field(5, keepdims))
matmul = (field(1, "one_hot") + field(1, "weights") + field(2, "prediction")
          + field(4, "MatMul"))
weights = [0.0] * (4 * 59)
for row in range(4):
    weights[row * 59 + 3] = 1.0
    weights[row * 59 + 27 + row] = 1.0
initializer = (field(1, 4) + field(1, 59) + field(2, 1) + field(8, "weights")
               + field(9, struct.pack("<236f", *weights)))
for batch in (1, 2):
    graph = (field(1, mean) + field(1, matmul) + field(2, "shape_protocol")
             + field(5, initializer)
             + field(11, tensor("pc", [batch, 3, 512]))
             + field(11, tensor("one_hot", [batch, 4]))
             + field(12, tensor("stage1_center", [batch, 3]))
             + field(12, tensor("prediction", [batch, 59])))
    model = field(1, 8) + field(2, "Autoware Bazel tests") + field(7, graph) + field(8, field(2, 13))
    Path(__file__).with_name(f"shape_batch{batch}.onnx").write_bytes(model)
