float4 main(float4 position : SV_POSITION) : SV_TARGET { return float4(position.zzz, 1.0f); }
