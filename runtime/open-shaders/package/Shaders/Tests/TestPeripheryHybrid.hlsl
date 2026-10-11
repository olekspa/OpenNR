// Exercises the exact RGB composition helper called by PeripheryHybridCS.hlsl.
#include "Upscaling/NeuralRendering/PeripheryHybridMath.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

/// @tags neural-rendering, periphery-hybrid
[numthreads(1, 1, 1)] void TestHybridCompositionMatchesCpuFixture()
{
	const float3 raw = float3(0.20, 0.35, 0.80);
	const float3 peripheryOutput = float3(0.50, 0.55, 0.90);
	const float3 centreOutput = float3(0.70, 0.75, 0.95);
	const float3 coarseP = float3(0.10, 0.12, 0.08);
	const float3 coarseC = float3(0.20, 0.15, 0.04);
	const float weight = PeripheryCentreWeight(40.0, 80.0);
	const float3 actual = PeripheryCompose(raw, peripheryOutput, centreOutput,
		coarseP, coarseC, weight, 0.83);
	// CPU reference evaluated with the same fixture, tolerance <= one normalized RGB LSB.
	const float3 expected = float3(0.533, 0.6146, 0.9314);
	ASSERT(IsTrue, all(abs(actual - expected) <= (1.0 / 255.0)));
}

/// @tags neural-rendering, periphery-hybrid
[numthreads(1, 1, 1)] void TestCentreRaisedCosineFeather()
{
	ASSERT(IsTrue, abs(PeripheryCentreWeight(0.0, 80.0)) < 0.0001);
	ASSERT(IsTrue, abs(PeripheryCentreWeight(40.0, 80.0) - 0.5) < 0.0001);
	ASSERT(IsTrue, abs(PeripheryCentreWeight(80.0, 80.0) - 1.0) < 0.0001);
}
