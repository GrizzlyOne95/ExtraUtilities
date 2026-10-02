#version 120

// Ogre-generated ParticleFX vertices are already converted for the active
// render system. Preserve their RGBA order instead of applying the native BZR
// sprite path's red/blue correction.

uniform mat4 wvpMat;
uniform vec4 diffuseColor;

attribute vec4 vertex;
attribute vec4 colour;
attribute vec2 uv0;

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vDepth;

void main()
{
    gl_Position = wvpMat * vertex;
    vColor = colour * diffuseColor;
    vTexCoord = uv0;
    vDepth = gl_Position.z;
}
