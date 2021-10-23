#include "colored_vertex.hlsli"

cbuffer matrices : register(b0)
{
	row_major float4x4 view;
	row_major float4x4 projection;
};

colored_vertex main(uint id : SV_VertexID, float3 position : POSITION)
{
	float3 colors[8] = {
		float3(0.098f, 0.098f, 0.439f),
		float3(0.0f, 0.392f, 0.0f),
		float3(1.0f, 0.0f, 0.0f),
		float3(1.0f, 0.843f, 0.0f),
		float3(0.0f, 1.0f, 0.0f),
		float3(0.0f, 1.0f, 1.0f),
		float3(1.0f, 0.0f, 1.0f),
		float3(1.0f, 0.714f, 0.757f),
	};

	colored_vertex vertex;
	vertex.position = mul(float4(position, 1.0f), mul(view, projection));
	vertex.color = colors[id % 8];
	return vertex;
}
