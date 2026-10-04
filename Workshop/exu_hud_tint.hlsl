// Shader model 3 twin of exu_hud_tint-sm4.hlsl; see exu_hud_tint.program.

void exu_hud_tint_vertex(
    uniform float4x4 wvpMat,
    uniform float4x4 texMat,
    uniform float4 tintColor,

    in float4 iPosition : POSITION,
    in float4 iTexCoord : TEXCOORD0,

    out float4 oPosition : POSITION,
    out float4 vColor : COLOR0,
    out float2 vTexCoord : TEXCOORD0
)
{
    oPosition = mul(wvpMat, iPosition);
    vColor = tintColor;
    vTexCoord = mul(texMat, iTexCoord).xy;
}
