float4 main(uint id : SV_VertexID) : SV_POSITION
{
	float2 vertices[]
		= {float2(-0.5f, -0.5f),
		   float2(0.0f, 0.5f),
		   float2(0.5f, -0.5f),
		   float2(-0.3f, -0.3f),
		   float2(0.2f, 0.7f),
		   float2(0.7f, -0.3f)};

	float depths[] = {0.3, 0.6};

	return float4(vertices[id % 6], depths[(id / 3) % 2], 1.0f);
}
