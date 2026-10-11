#ifndef OPENNR_PERIPHERY_HYBRID_MATH_HLSLI
#define OPENNR_PERIPHERY_HYBRID_MATH_HLSLI

float PeripheryCentreWeight(float edgeDistancePx, float featherWidthPx)
{
	const float t = saturate(edgeDistancePx / max(featherWidthPx, 1.0));
	return 0.5 - 0.5 * cos(3.14159265359 * t);
}

float PeripheryInsideDistance(float2 pixelCenter, float2 cropMin, float2 cropMax, float radius)
{
	const float2 center = 0.5 * (cropMin + cropMax);
	const float2 halfSize = 0.5 * (cropMax - cropMin);
	const float r = min(max(radius, 0.0), min(halfSize.x, halfSize.y));
	const float2 q = abs(pixelCenter - center) - (halfSize - r);
	const float signedDistance = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
	return -signedDistance - 0.5;
}

float3 PeripheryCompose(float3 raw, float3 peripheryOutput, float3 centreOutput,
	float3 coarsePeriphery, float3 coarseCentre, float centreWeight, float lightingGain)
{
	const float3 effP = peripheryOutput - raw;
	const float3 effC = centreOutput - raw;
	const float w = saturate(centreWeight);
	return raw + clamp(lightingGain, 0.0, 1.5) * coarsePeriphery +
		w * (effC - coarseCentre) + (1.0 - w) * (effP - coarsePeriphery);
}

#endif
