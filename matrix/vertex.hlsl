float4 main(uint id : SV_VertexID) : SV_POSITION
{
	float2 vertices[] = {float2(-0.5f, -0.5f), float2(0.5f, -0.5f), float2(0.0f, 0.5f)};
	return float4(vertices[id % 3], 0.0f, 0.0f);
}