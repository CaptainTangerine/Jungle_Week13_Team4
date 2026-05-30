#include "PostProcess/DOF/DOFCommon.hlsli"

Texture2D SceneTexture : register(t0);
Texture2D DepthTexture : register(t1);

// rgb = scene color, a = signed CoC.
float4 PS(PS_Input_UV input) : SV_TARGET
{
    float3 color = SceneTexture.SampleLevel(LinearClampSampler, input.uv, 0).rgb;
    float depth = DepthTexture.SampleLevel(PointClampSampler, input.uv, 0).r;
    float coc = ComputeCoC(LinearizeDepth(depth));
    return float4(color, coc);
}
