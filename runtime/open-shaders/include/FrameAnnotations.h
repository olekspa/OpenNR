#pragma once

#include <cstdint>

namespace FrameAnnotations
{
	void OnPostPostLoad();
	void OnDataLoaded();
	void OnMapMenuState(bool open, std::uint32_t frame);
	void OnMapShaderDraw(std::uint16_t effect, std::uint32_t frame, bool routedDisplayRes);
	void OnMapRenderTargetBind();
}
