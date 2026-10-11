"""Source contracts for the VR grass input-layout descriptor path."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
CPP = ROOT / "src" / "Features" / "GrassOptimizations.cpp"
SIGNATURE = ROOT / "features" / "Grass Optimizations" / "Shaders" / "GrassOptimizations" / "GrassInstanceSignatureVS.hlsl"


class GrassInputLayoutContractTests(unittest.TestCase):
    def test_grass_vertex_projection_uses_the_active_eye_camera_matrix(self):
        grass = (ROOT / "package" / "Shaders" / "RunGrass.hlsl").read_text(encoding="utf-8")
        self.assertIn("mul(FrameBuffer::CameraViewProj[CurrentEyeIndex], float4(eyeRel, 1.0))", grass)
        self.assertNotIn("mul(WorldViewProj[CurrentEyeIndex], msPosition)", grass)

    def test_live_descriptor_has_no_color_and_signature_does_not_require_optional_attributes(self):
        # BSGraphics::VertexDesc::HasFlag reads bits 44..59. The reported descriptor
        # has VF_VERTEX|VF_UV|VF_NORMAL|VF_TANGENT (0x1b), but not VF_COLORS (0x20).
        descriptor = int("8001B00000650487", 16)
        flags = (descriptor >> 44) & 0xFFFF
        self.assertEqual(flags & 0x1B, 0x1B)
        self.assertEqual(flags & 0x20, 0)

        source = SIGNATURE.read_text(encoding="utf-8")
        self.assertIn("POSITION0", source)
        for semantic in ("TEXCOORD4", "TEXCOORD5", "TEXCOORD6", "TEXCOORD7"):
            self.assertIn(semantic, source)
        for optional_semantic in ("TEXCOORD0", "NORMAL0", "COLOR0"):
            self.assertNotIn(optional_semantic, source)

    def test_null_layout_entries_negative_cache_creation_and_signature_failures(self):
        source = CPP.read_text(encoding="utf-8")
        method = source.split("ID3D11InputLayout* GrassOptimizations::GetOptimizedInputLayout", 1)[1]
        method = method.split("ID3D11ComputeShader* GrassOptimizations::GetCullCS", 1)[0]
        cache = (ROOT / "src" / "Features" / "GrassOptimizations" / "InputLayoutCache.h").read_text(encoding="utf-8")

        # The early lookup returns either the successful layout or nullptr for a
        # previously cached failure; neither path invokes shader/layout creation.
        self.assertIn("if (auto* cached = inputLayouts.Find(a_descVal))", method)
        self.assertGreaterEqual(method.count("inputLayouts.CacheFailure(a_descVal)"), 2)
        self.assertIn("inputLayouts.CacheSuccess(a_descVal, std::move(layout))", method)
        self.assertIn("CreateInputLayout(elements.data(), count", method)
        self.assertIn("failure cached", method)
        self.assertIn("inputLayouts.Clear()", source)
        self.assertIn("entries.try_emplace(descriptor, Resource{})", cache)

    def test_descriptor_attribute_semantics_remain_descriptor_driven(self):
        source = CPP.read_text(encoding="utf-8")
        self.assertIn("if (vdesc.HasFlag(RE::BSGraphics::Vertex::VF_UV))", source)
        self.assertIn("if (vdesc.HasFlag(RE::BSGraphics::Vertex::VF_NORMAL))", source)
        self.assertIn("if (vdesc.HasFlag(RE::BSGraphics::Vertex::VF_COLORS))", source)
        self.assertIn("GetAttributeOffset(RE::BSGraphics::Vertex::VA_NORMAL)", source)
        self.assertIn("GetAttributeOffset(RE::BSGraphics::Vertex::VA_COLOR)", source)


if __name__ == "__main__":
    unittest.main()
