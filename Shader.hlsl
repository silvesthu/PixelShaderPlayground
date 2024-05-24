
// Macro inputs:
//	TARGET_SIZE_X
//	TARGET_SIZE_Y
//	WAVE_LANE_COUNT_MIN
//  WAVE_LANE_COUNT_MAX
//  TOTAL_LANE_COUNT

// Generate screen space triangle
// From https://anteru.net/blog/2012/minimal-setup-screen-space-quads-no-buffers-layouts-required/
float4 vs_main(uint id : SV_VertexID) : SV_POSITION
{
	float x = float ((id & 2) << 1) - 1.0;
	float y = 1.0 - float ((id & 1) << 2);
	return float4 (x, y, 0, 1);
}

static Texture2D<float4> Texture = ResourceDescriptorHeap[0];
SamplerState s0 : register(s0);
[RootSignature("RootFlags(CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED), StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_LINEAR)")]
float4 ps_main_a(float4 position : SV_POSITION) : SV_Target0
{
    float4 ret      = 0;
    bool cond       = position.x < 0;
    [branch]
    if (QuadAny(cond))
    {
        float4 sampled_result = Texture.Sample(s0, (position.xy + 0.5) / float2(TARGET_SIZE_X, TARGET_SIZE_Y));
        if (cond)
            ret = sampled_result + 1;
    }
    return ret;
}

float4 ps_main_b(float4 position : SV_POSITION) : SV_Target0
{
    float4 ret      = 0;
    bool cond       = position.x < 0;
    [branch]
    if (cond)
    {
        float4 sampled_result = Texture.Sample(s0, (position.xy + 0.5) / float2(TARGET_SIZE_X, TARGET_SIZE_Y));
        if (cond)
            ret = sampled_result + 1;
    }
    return ret;
}
