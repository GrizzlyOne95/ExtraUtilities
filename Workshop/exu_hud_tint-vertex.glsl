#version 120

// GLSL twin of exu_hud_tint-sm4.hlsl; see exu_hud_tint.program.

uniform mat4 wvpMat;
uniform mat4 texMat;
uniform vec4 tintColor;

attribute vec4 vertex;
attribute vec4 uv0;

varying vec4 vColor;
varying vec2 vTexCoord;

void main()
{
    gl_Position = wvpMat * vertex;
    vColor = tintColor;
    vTexCoord = vec2(texMat * uv0);
}
