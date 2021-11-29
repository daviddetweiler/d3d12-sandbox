#include "colored_vertex.hlsli"

cbuffer matrices : register(b0)
{
	row_major float4x4 view;
	row_major float4x4 projection;
};

colored_vertex main(uint id : SV_VertexID, float3 position : POSITION, float3 normal : NORMAL)
{
	colored_vertex vertex;
	vertex.position = mul(float4(position, 1.0f), mul(view, projection));
	vertex.color = abs(normal);
	return vertex;
}
