#include "vertex_data.hlsli"

float4 main(vertex_data vertex) : SV_TARGET
{
	const float3 normal = vertex.normal;
	const float3 color = clamp(dot(normal, float3(0.0f, 1.0f, 0.0f)) * 0.8f, 0.01f, 1.0f).xxx;
	return float4(color, 1.0f);
}