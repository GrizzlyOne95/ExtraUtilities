// Ogre-generated billboards use RenderSystem::convertColourValue when filling
// VET_COLOUR. On the D3D11 renderer that is already presented to the shader as
// RGBA, unlike BZR's native packed sprite vertices. Do not apply the stock
// effect-sm4.hlsl BGRA correction a second time here.

void exu_ogre_particle_vertex(
    uniform float4x4 wvpMat,
    uniform float4 diffuseColor,

    in float4 iPosition : POSITION,
    in float4 iColor : COLOR0,
    in float2 iTexCoord : TEXCOORD0,

    out float4 vColor : COLOR0,
    out float2 vTexCoord : TEXCOORD0,
    out float vDepth : TEXCOORD1,

    out float4 oPosition : SV_POSITION
)
{
    oPosition = mul(wvpMat, iPosition);
    vColor = iColor * diffuseColor;
    vTexCoord = iTexCoord;
    vDepth = oPosition.z;
}
