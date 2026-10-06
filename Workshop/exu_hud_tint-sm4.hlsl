// See exu_hud_tint.program. The pass diffuse is an Ogre ColourValue uniform,
// already RGBA, so no BGRA swizzle is applied (unlike packed vertex colour).

void exu_hud_tint_vertex(
    uniform float4x4 wvpMat,
    uniform float4x4 texMat,
    uniform float4 tintColor,

    in float4 iPosition : POSITION,
    in float4 iTexCoord : TEXCOORD0,

    out float4 oPosition : SV_POSITION,
    out float4 vColor : COLOR0,
    out float2 vTexCoord : TEXCOORD0
)
{
    oPosition = mul(wvpMat, iPosition);
    vColor = tintColor;
    vTexCoord = mul(texMat, iTexCoord).xy;
}

void exu_hud_tint_fragment(
    uniform Texture2D diffuseMap : register(t0),
    uniform SamplerState diffuseSam : register(s0),

    in float4 iPosition : SV_POSITION,
    in float4 vColor : COLOR0,
    in float2 vTexCoord : TEXCOORD0,

    out float4 oColor : SV_TARGET
)
{
    oColor = diffuseMap.Sample(diffuseSam, vTexCoord) * vColor;
}

void exu_hud_text_vertex(
    uniform float4x4 wvpMat,

    in float4 iPosition : POSITION,
    in float4 iColor : COLOR0,
    in float2 iTexCoord : TEXCOORD0,

    out float4 oPosition : SV_POSITION,
    out float4 vColor : COLOR0,
    out float2 vTexCoord : TEXCOORD0
)
{
    oPosition = mul(wvpMat, iPosition);
    vColor = iColor;
    vTexCoord = iTexCoord;
}
