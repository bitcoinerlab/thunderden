"""Fix the recognizer's input geometry for OpenCV, preserving trained weights.

The source models and resulting artifacts must match pinned SHA-256 hashes.
Compare the original and simplified graph numerically using build-only ORT.
"""
import hashlib
import os
from pathlib import Path
import sys

import numpy as np
import onnx
import onnxruntime as ort
import onnxsim

directory = Path(sys.argv[1])
def verify(path, variable):
    assert hashlib.sha256(path.read_bytes()).hexdigest() == os.environ[variable], variable

verify(directory / "det.onnx", "OCR_DET_SHA256")
source = directory / "rec-original.onnx"
verify(source, "OCR_REC_SHA256")
model = onnx.load(source)
characters = next(item.value for item in model.metadata_props if item.key == "character")
(directory / "characters.txt").write_bytes(characters.encode())
verify(directory / "characters.txt", "OCR_CHARACTERS_SHA256")
fixed, valid = onnxsim.simplify(model, overwrite_input_shapes={model.graph.input[0].name: [1, 3, 48, 640]})
assert valid
onnx.save(fixed, directory / "rec.onnx")
verify(directory / "rec.onnx", "OCR_FIXED_REC_SHA256")
options = ort.SessionOptions()
options.intra_op_num_threads = options.inter_op_num_threads = 1
original = ort.InferenceSession(str(source), options, providers=["CPUExecutionProvider"])
simplified = ort.InferenceSession(str(directory / "rec.onnx"), options, providers=["CPUExecutionProvider"])
for seed in range(3):
    data = np.random.default_rng(seed).uniform(-1, 1, (1, 3, 48, 640)).astype(np.float32)
    a = original.run(None, {"x": data})[0]
    b = simplified.run(None, {"x": data})[0]
    np.testing.assert_allclose(a, b, rtol=1e-4, atol=1e-5)
print("PASS: pinned OCR models, character dictionary and fixed-shape numerical equivalence")
