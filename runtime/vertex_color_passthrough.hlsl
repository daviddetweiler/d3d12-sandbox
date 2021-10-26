#include "colored_vertex.hlsli"

float4 main(colored_vertex vertex) : SV_TARGET
{
	return float4(vertex.color, 1.0f);
}