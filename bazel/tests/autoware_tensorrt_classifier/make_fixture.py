# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Write a tiny ONNX ReduceMean fixture without a model-generation dependency.

ONNX IR 8 / opset 13: float [2,3,2,2] -> spatial mean -> float [2,3].
Wire field numbers follow onnx/onnx.proto; TensorRT parses it in the native gate.
"""
from pathlib import Path


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


axes = field(1, "axes") + field(8, 2) + field(8, 3) + field(20, 7)
keepdims = field(1, "keepdims") + field(3, 0) + field(20, 2)
node = (field(1, "image") + field(2, "scores") + field(4, "ReduceMean")
        + field(5, axes) + field(5, keepdims))
graph = (field(1, node) + field(2, "channel_means")
         + field(11, tensor("image", [2, 3, 2, 2]))
         + field(12, tensor("scores", [2, 3])))
model = field(1, 8) + field(2, "Autoware Bazel tests") + field(7, graph) + field(8, field(2, 13))
Path(__file__).with_name("channel_means.onnx").write_bytes(model)
