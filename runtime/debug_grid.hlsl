cbuffer matrices : register(b0)
{
	row_major float4x4 view;
	row_major float4x4 projection;
};

float4 main(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID) : SV_POSITION
{
	bool horizontal = (instance_id / 9) % 2;
	instance_id = instance_id % 9;
	const float2 line_ends[] = {float2(-5.0f, 0.0f), float2(5.0f, 0.0f)};
	const float offset = 1.0f;
	const float2 offset_vector
		= float2(0.0f, offset * (instance_id / 2 + instance_id % 2) * (instance_id % 2 ? 1 : -1));

	const float4 vertex = float4(line_ends[vertex_id % 2] + offset_vector, 0.0f, 1.0f);

	return mul(horizontal ? vertex : vertex.yxzw, mul(view, projection));
}
