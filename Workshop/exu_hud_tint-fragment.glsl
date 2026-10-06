#version 120

// GLSL twin of exu_hud_tint_fragment; see exu_hud_tint.program.

uniform sampler2D diffuseMap;

varying vec4 vColor;
varying vec2 vTexCoord;

void main()
{
    gl_FragColor = texture2D(diffuseMap, vTexCoord) * vColor;
}
