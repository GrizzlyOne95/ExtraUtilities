// See exu_hud_tint.program. The pass diffuse is an Ogre ColourValue uniform,
// already RGBA, so no BGRA swizzle is applied (unlike packed vertex colour).

void exu_hud_tint_vertex(
    uniform float4x4 wvpMat,
    uniform float4x4 texMat,
    uniform float4 tintColor,

    in float4 iPosition : POSITION,
    in float4 iTexCoord : TEXCOORD0,

    out float4 vColor : COLOR0,
    out float2 vTexCoord : TEXCOORD0,

    out float4 oPosition : SV_POSITION
)
{
    oPosition = mul(wvpMat, iPosition);
    vColor = tintColor;
    vTexCoord = mul(texMat, iTexCoord).xy;
}
