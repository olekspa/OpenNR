import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tools.tensorrt_student import (
    ENGINE_BUILD_CONFIG,
    engine_cache_is_reusable,
    onnx_cache_is_reusable,
    sha256_file,
    validate_residual_gain,
    write_json_atomic,
)


class ResidualGainValidationTests(unittest.TestCase):
    def test_accepts_finite_nonnegative_gains(self):
        for gain in (0, 0.25, 1, 4.5):
            with self.subTest(gain=gain):
                self.assertEqual(validate_residual_gain(gain), float(gain))

    def test_rejects_negative_and_nonfinite_gains(self):
        for gain in (-1, float('nan'), float('inf'), float('-inf')):
            with self.subTest(gain=gain):
                with self.assertRaises(ValueError):
                    validate_residual_gain(gain)


class OnnxCacheTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.onnx = self.root / 'student.onnx'
        self.manifest = self.root / 'source.json'
        self.onnx.write_bytes(b'current exported graph')
        self.expected = {
            'checkpoint': 'student.pt',
            'checkpoint_sha256': 'a' * 64,
            'resolution': [256, 256],
            'residual_gain': 1.0,
            'exporter_sha256': 'e' * 64,
            'student_implementation_sha256': 'f' * 64,
            'torch_version': '2.7.1',
            'opset_version': 17,
        }

    def test_existing_onnx_without_manifest_is_reexported(self):
        self.assertFalse(onnx_cache_is_reusable(self.onnx, self.manifest, self.expected))

    def test_matching_provenance_and_onnx_hash_allows_reuse(self):
        write_json_atomic(self.manifest, {**self.expected, 'onnx_sha256': sha256_file(self.onnx)})
        self.assertTrue(onnx_cache_is_reusable(self.onnx, self.manifest, self.expected))

    def test_changed_source_inputs_are_rejected(self):
        write_json_atomic(self.manifest, {**self.expected, 'onnx_sha256': sha256_file(self.onnx)})
        with self.assertRaisesRegex(ValueError, 'ONNX provenance changed'):
            onnx_cache_is_reusable(self.onnx, self.manifest, {**self.expected, 'residual_gain': 2.0})

    def test_missing_or_stale_artifact_hash_forces_reexport(self):
        write_json_atomic(self.manifest, self.expected)
        self.assertFalse(onnx_cache_is_reusable(self.onnx, self.manifest, self.expected))
        write_json_atomic(self.manifest, {**self.expected, 'onnx_sha256': 'b' * 64})
        self.assertFalse(onnx_cache_is_reusable(self.onnx, self.manifest, self.expected))

    def test_valid_json_that_is_not_an_object_forces_reexport(self):
        for value in ([], 'manifest', None):
            with self.subTest(value=value):
                self.manifest.write_text(json.dumps(value))
                self.assertFalse(onnx_cache_is_reusable(self.onnx, self.manifest, self.expected))

    def test_exporter_model_torch_or_opset_change_invalidates_cache(self):
        write_json_atomic(self.manifest, {**self.expected, 'onnx_sha256': sha256_file(self.onnx)})
        for key, changed in (
            ('exporter_sha256', '1' * 64),
            ('student_implementation_sha256', '2' * 64),
            ('torch_version', 'other'),
            ('opset_version', 18),
        ):
            with self.subTest(key=key):
                with self.assertRaisesRegex(ValueError, 'ONNX provenance changed'):
                    onnx_cache_is_reusable(self.onnx, self.manifest, {**self.expected, key: changed})


class EngineCacheTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.engine = self.root / 'student.engine'
        self.manifest = self.root / 'engine_source.json'
        self.engine.write_bytes(b'serialized engine')
        self.version = '10.13.3'
        self.onnx_hash = 'c' * 64
        self.hardware_identity = {'name': 'Example GPU', 'compute_capability': [9, 0]}
        write_json_atomic(self.manifest, {
            'tensorrt_version': self.version,
            'onnx_sha256': self.onnx_hash,
            'build_config': ENGINE_BUILD_CONFIG,
            'hardware_identity': self.hardware_identity,
            'engine_sha256': sha256_file(self.engine),
        })

    def reusable(self, **overrides):
        args = dict(tensorrt_version=self.version, onnx_sha256=self.onnx_hash, hardware_identity=self.hardware_identity)
        args.update(overrides)
        return engine_cache_is_reusable(self.engine, self.manifest, **args)

    def test_matching_version_config_hashes_allows_reuse(self):
        self.assertTrue(self.reusable())

    def test_changed_tensorrt_version_or_onnx_hash_forces_rebuild(self):
        self.assertFalse(self.reusable(tensorrt_version='10.14.0'))
        self.assertFalse(self.reusable(onnx_sha256='d' * 64))

    def test_changed_build_config_or_engine_bytes_forces_rebuild(self):
        metadata = json.loads(self.manifest.read_text())
        metadata['build_config'] = {**ENGINE_BUILD_CONFIG, 'workspace_bytes': 1}
        self.manifest.write_text(json.dumps(metadata))
        self.assertFalse(self.reusable())
        metadata['build_config'] = ENGINE_BUILD_CONFIG
        self.manifest.write_text(json.dumps(metadata))
        self.engine.write_bytes(b'different engine')
        self.assertFalse(self.reusable())

    def test_changed_hardware_identity_forces_rebuild(self):
        self.assertFalse(self.reusable(hardware_identity={'name': 'Other GPU', 'compute_capability': [9, 0]}))

    def test_valid_json_that_is_not_an_object_forces_rebuild(self):
        for value in ([], 'manifest', None):
            with self.subTest(value=value):
                self.manifest.write_text(json.dumps(value))
                self.assertFalse(self.reusable())


class ImportIsolationTests(unittest.TestCase):
    def test_cache_helpers_import_without_ml_or_gpu_packages(self):
        check = (
            'import sys; import tools.tensorrt_student; '
            "assert not any(name == 'torch' or name.startswith('torch.') "
            "or name == 'tensorrt' or name.startswith('tensorrt.') "
            "for name in sys.modules)"
        )
        subprocess.run([sys.executable, '-c', check], check=True)


if __name__ == '__main__':
    unittest.main()
