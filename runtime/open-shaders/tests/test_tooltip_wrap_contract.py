import unittest
from pathlib import Path


ROOT = Path(__file__).parents[1]
UI_CPP = (ROOT / "src" / "Utils" / "UI.cpp").read_text(encoding="utf-8")
UI_H = (ROOT / "src" / "Utils" / "UI.h").read_text(encoding="utf-8")


class TooltipWrapContractTests(unittest.TestCase):
    def test_shared_hover_tooltip_uses_scaled_readable_width(self):
        self.assertIn("360.0f * GetUIScale()", UI_CPP)
        self.assertIn("viewportWidth * 0.5f", UI_CPP)
        self.assertIn("ImGui::SetNextWindowSizeConstraints(ImVec2(tooltipMaxWidth, 0.0f)", UI_CPP)
        self.assertIn("ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrapWidth)", UI_CPP)
        self.assertIn("class HoverTooltipWrapper", UI_H)


if __name__ == "__main__":
    unittest.main()
