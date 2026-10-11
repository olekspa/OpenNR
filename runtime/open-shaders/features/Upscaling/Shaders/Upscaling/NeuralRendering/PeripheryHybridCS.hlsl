#include "Upscaling/NeuralRendering/PeripheryHybridMath.hlsli"

cbuffer Params : register(b0)
{
	uint Width;
	uint Height;
	uint EyeWidth;
	uint Mode; // 0: low-pass effects to 1/8, 1: hybrid composition
	uint LeftCropX;
	uint LeftCropY;
	uint RightCropX;
	uint RightCropY;
	uint CropWidth;
	uint CropHeight;
	float PeripheryGain;
	float FeatherWidth;
};

Texture2D<float4> Raw : register(t0);
Texture2D<float4> Periphery : register(t1);
Texture2D<float4> Centre : register(t2);
Texture2D<float4> PeripheryCoarse : register(t3);
Texture2D<float4> CentreCoarse : register(t4);
Texture2D<float4> PeripheryLowResResidual : register(t5);
RWTexture2D<float4> PeripheryCoarseOut : register(u0);
RWTexture2D<float4> CentreCoarseOut : register(u1);
RWTexture2D<float4> Output : register(u2);
SamplerState LinearClamp : register(s0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (Mode == 0)
	{
		const uint2 coarseSize = uint2((Width + 7u) / 8u, (Height + 7u) / 8u);
		if (id.x >= coarseSize.x || id.y >= coarseSize.y)
			return;
		float3 p = 0.0;
		float3 c = 0.0;
		uint lowWidth, lowHeight;
		PeripheryLowResResidual.GetDimensions(lowWidth, lowHeight);
		const uint eyeCoarseWidth = EyeWidth / 8u;
		const uint eye = id.x / eyeCoarseWidth;
		const uint coarseLocalX = id.x % eyeCoarseWidth;
		const uint cropX = eye == 0u ? LeftCropX : RightCropX;
		const uint cropY = eye == 0u ? LeftCropY : RightCropY;
		const float lowEyeWidth = float(lowWidth) * 0.5;
		const float displayX0 = float(coarseLocalX * 8u);
		const float displayX1 = min(displayX0 + 8.0, float(EyeWidth));
		const float displayY0 = float(id.y * 8u);
		const float displayY1 = min(displayY0 + 8.0, float(Height));
		const float lowX0 = displayX0 * lowEyeWidth / float(EyeWidth);
		const float lowX1 = displayX1 * lowEyeWidth / float(EyeWidth);
		const float lowY0 = displayY0 * float(lowHeight) / float(Height);
		const float lowY1 = displayY1 * float(lowHeight) / float(Height);
		const uint firstX = min(uint(floor(lowX0)), uint(lowEyeWidth) - 1u);
		const uint endX = min(uint(ceil(lowX1)), uint(lowEyeWidth));
		const uint firstY = min(uint(floor(lowY0)), lowHeight - 1u);
		const uint endY = min(uint(ceil(lowY1)), lowHeight);
		float weightSum = 0.0;
		[loop]
		for (uint ly = firstY; ly < endY; ++ly) {
			const float wy = max(0.0, min(lowY1, float(ly + 1u)) - max(lowY0, float(ly)));
			[loop]
			for (uint lx = firstX; lx < endX; ++lx) {
				const float wx = max(0.0, min(lowX1, float(lx + 1u)) - max(lowX0, float(lx)));
				const float weight = wx * wy;
				p += PeripheryLowResResidual.Load(int3(eye * uint(lowEyeWidth) + lx, ly, 0)).rgb * weight;
				weightSum += weight;
			}
		}
		p /= max(weightSum, 1e-6);
		if (!all(isfinite(p))) p = 0.0;
		const uint cropCoarseMinX = cropX / 8u > 0u ? cropX / 8u - 1u : 0u;
		const uint cropCoarseEndX = min((cropX + CropWidth + 7u) / 8u + 1u, eyeCoarseWidth);
		const uint cropCoarseMinY = cropY / 8u > 0u ? cropY / 8u - 1u : 0u;
		const uint cropCoarseEndY = min((cropY + CropHeight + 7u) / 8u + 1u, coarseSize.y);
		const bool centreCoarseNeeded =
			coarseLocalX >= cropCoarseMinX && coarseLocalX < cropCoarseEndX &&
			id.y >= cropCoarseMinY && id.y < cropCoarseEndY;
		if (centreCoarseNeeded) {
			[unroll]
			for (uint y = 0; y < 8; ++y) {
				[unroll]
				for (uint x = 0; x < 8; ++x) {
					const uint2 q = min(id.xy * 8u + uint2(x, y), uint2(Width - 1u, Height - 1u));
					const uint pixelEye = q.x / EyeWidth;
					const uint localQx = q.x - pixelEye * EyeWidth;
					const uint pixelCropX = pixelEye == 0u ? LeftCropX : RightCropX;
					const uint pixelCropY = pixelEye == 0u ? LeftCropY : RightCropY;
					if (localQx >= pixelCropX && localQx < pixelCropX + CropWidth &&
						q.y >= pixelCropY && q.y < pixelCropY + CropHeight) {
						const float3 raw = Raw.Load(int3(q, 0)).rgb;
						c += Centre.Load(int3(q, 0)).rgb - raw;
					}
				}
			}
		}
		PeripheryCoarseOut[id.xy] = float4(p, 0.0);
		CentreCoarseOut[id.xy] = float4(c / 64.0, 0.0);
		return;
	}
	if (id.x >= Width || id.y >= Height)
		return;
	const uint2 q = id.xy;
	const float4 rawSample = Raw.Load(int3(q, 0));
	const float3 raw = rawSample.rgb;
	// ModelResolutionCS mode 4 stores raw + bilinear-upsample(Pout - Pin),
	// so subtracting raw here yields the required effP in display pixels.
	const uint2 coarseSize = uint2((Width + 7u) / 8u, (Height + 7u) / 8u);
	const uint eye = q.x / EyeWidth;
	const float coarseEyeWidth = float(coarseSize.x) * 0.5;
	const float coarseX = clamp(float(eye) * coarseEyeWidth + (float(q.x % EyeWidth) + 0.5) * coarseEyeWidth / float(EyeWidth),
		float(eye) * coarseEyeWidth + 0.5, float(eye + 1u) * coarseEyeWidth - 0.5);
	const float coarseY = clamp((float(q.y) + 0.5) * 0.125, 0.5, float(coarseSize.y) - 0.5);
	const float2 coarseUV = float2(coarseX / float(coarseSize.x), coarseY / float(coarseSize.y));
	const float3 coarseP = PeripheryCoarse.SampleLevel(LinearClamp, coarseUV, 0).rgb;
	const uint localX = q.x % EyeWidth;
	const uint cropX = eye == 0u ? LeftCropX : RightCropX;
	const uint cropY = eye == 0u ? LeftCropY : RightCropY;
	const float borderExtension = 4.0 * FeatherWidth + 64.0;
	const float2 cropMin = float2(cropX == 0u ? -borderExtension : float(cropX),
		cropY == 0u ? -borderExtension : float(cropY));
	const float2 cropMax = float2(cropX + CropWidth >= EyeWidth ? float(EyeWidth) + borderExtension : float(cropX + CropWidth),
		cropY + CropHeight >= Height ? float(Height) + borderExtension : float(cropY + CropHeight));
	const float edgeDistance = PeripheryInsideDistance(float2(localX, q.y) + 0.5, cropMin, cropMax,
		min(FeatherWidth, 0.25 * min(float(CropWidth), float(CropHeight))));
	const float w = edgeDistance > 0.0 ? PeripheryCentreWeight(edgeDistance, FeatherWidth) : 0.0;
	float3 result;
	if (w > 0.0) {
		const float3 coarseC = CentreCoarse.SampleLevel(LinearClamp, coarseUV, 0).rgb;
		const float3 centreOutput = Centre.Load(int3(q, 0)).rgb;
		if (w >= 1.0) {
			result = raw + clamp(PeripheryGain, 0.0, 1.5) * coarseP + ((centreOutput - raw) - coarseC);
		} else {
			const float3 peripheryOutput = Periphery.Load(int3(q, 0)).rgb;
			result = PeripheryCompose(raw, peripheryOutput, centreOutput, coarseP, coarseC, w, PeripheryGain);
		}
	} else {
		const float3 peripheryOutput = Periphery.Load(int3(q, 0)).rgb;
		const float3 effP = peripheryOutput - raw;
		result = raw + clamp(PeripheryGain, 0.0, 1.5) * coarseP + (effP - coarseP);
	}
	if (!all(isfinite(result))) result = raw;
	Output[q] = float4(result, rawSample.a);
}
