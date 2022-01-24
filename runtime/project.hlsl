#include "vertex_data.hlsli"

cbuffer matrices : register(b0)
{
	row_major float4x4 view;
	row_major float4x4 projection;
};

vertex_data main(vertex_data vertex)
{
	vertex.position = mul(vertex.position + float4(vertex.offset, 0.0), mul(view, projection));
	return vertex;
}
