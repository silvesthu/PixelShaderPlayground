
// Inputs:
//	THREAD_GROUP_SIZE_X
//	THREAD_GROUP_SIZE_Y
//	THREAD_GROUP_SIZE_Z
//	THREAD_GROUP_SIZE
//	DISPATCH_SIZE
//	WAVE_LANE_COUNT_MIN

// Generate screen space triangle
// From https://anteru.net/blog/2012/minimal-setup-screen-space-quads-no-buffers-layouts-required/
float4 vs_main(uint id : SV_VertexID) : SV_POSITION
{
	float x = float ((id & 2) << 1) - 1.0;
	float y = 1.0 - float ((id & 1) << 2);
	return float4 (x, y, 0, 1);
}

[RootSignature("RootFlags(0), UAV(u0)")]
float4 ps_main(float4 position : SV_POSITION) : SV_Target0
{
	return position;
}
