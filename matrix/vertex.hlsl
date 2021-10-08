float4 main(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID) : SV_POSITION
{
	const float2 line_ends[] = {float2(-1.0f, 0.0f), float2(1.0f, 0.0f)};
	const float offset = 0.2f;
	const float2 offset_vector
		= float2(0.0f, offset * (instance_id / 2 + instance_id % 2) * (instance_id % 2 ? 1 : -1));

	return float4(line_ends[vertex_id % 2] + offset_vector, 0.0f, 1.0f);
}
