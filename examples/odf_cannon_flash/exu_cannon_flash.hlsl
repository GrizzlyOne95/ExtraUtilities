// Original layered cannon flash. EXU supplies per-renderable simulation phase
// and a stable shot seed; no wall clock or gameplay RNG is used.
float4x4 worldViewProj;
float4 flashParams; // overall brightness, turbulence, noise frequency, edge exponent
float3 flashCoolColour;
float3 flashHotColour;
float3 cameraPosition; // Ogre camera_position_object_space
float4 flashState; // normalized age, shot seed, valid marker, duration
float4 flashAnimation; // axial roll radians, length variation, growth phase, fade exponent

#ifdef EXU_DX9
#define CLIP_POSITION POSITION
#define FRAGMENT_TARGET COLOR0
sampler2D flameSampler : register(s0);
float4 flameSample(float2 uv) { return tex2D(flameSampler, uv); }
#else
#define CLIP_POSITION SV_POSITION
#define FRAGMENT_TARGET SV_Target
Texture2D flameTexture : register(t0);
SamplerState flameSampler : register(s0);
float4 flameSample(float2 uv) { return flameTexture.Sample(flameSampler, uv); }
#endif

struct VertexInput {
    float4 position : POSITION;
    float4 colour : COLOR0;
    float2 uv : TEXCOORD0;
};
struct PixelInput {
    float4 position : CLIP_POSITION;
    float4 colour : COLOR0;
    float2 uv : TEXCOORD0;
    float3 localPosition : TEXCOORD1;
    float layer : TEXCOORD2;
};

PixelInput flashVertex(VertexInput input) {
    PixelInput result;
    float phase = flashState.z > .5 ? saturate(flashState.x) : .15;
    float seed = flashState.z > .5 ? flashState.y : .5;
    float roll = (seed-.5)*flashAnimation.x;
    float s = sin(roll), c = cos(roll);
    float4 local = input.position;
    local.xy = float2(c*local.x-s*local.y, s*local.x+c*local.y);
    // Every transform is about the authored muzzle origin. Never translate
    // the root or change the native weapon/projectile matrix.
    local.z *= (1+flashAnimation.y*(2*frac(seed*13.17)-1)) *
        lerp(.68,1.0,smoothstep(0,max(.01,flashAnimation.z),phase));
    result.position = mul(worldViewProj, local);
    result.colour = input.colour;
    result.layer = floor(input.uv.x * .5);
    result.uv = float2(input.uv.x - result.layer*2, input.uv.y);
    result.localPosition = local.xyz;
    return result;
}

float hash3(float3 p) {
    p = frac(p * .1031);
    p += dot(p, p.yzx + 33.33);
    return frac((p.x + p.y) * p.z);
}

float noise3(float3 p) {
    float3 i = floor(p), f = frac(p);
    f = f*f*(3-2*f);
    return lerp(lerp(lerp(hash3(i), hash3(i+float3(1,0,0)), f.x),
                     lerp(hash3(i+float3(0,1,0)), hash3(i+float3(1,1,0)), f.x), f.y),
                lerp(lerp(hash3(i+float3(0,0,1)), hash3(i+float3(1,0,1)), f.x),
                     lerp(hash3(i+float3(0,1,1)), hash3(i+float3(1,1,1)), f.x), f.y), f.z);
}

float4 flashFragment(PixelInput input) : FRAGMENT_TARGET {
    float phase = flashState.z > .5 ? saturate(flashState.x) : .15;
    float seed = flashState.z > .5 ? flashState.y : .5;
    float3 p = input.localPosition * flashParams.z + float3(seed*31,seed*17,phase*2);
    float turbulent = .57*noise3(p) + .29*noise3(p*2.07+13.1) + .14*noise3(p*4.13-7.2);
    float heat = saturate((input.colour.b - .035) / .93);
    // Fine cool gaps break up the gas sheets; the hot core stays continuous.
    float breakup = lerp(1, smoothstep(.20, .72, turbulent), flashParams.y*(1-.65*heat));
    float density = pow(saturate(input.colour.a), flashParams.w) * breakup;
    float2 uv = float2(input.uv.x,1-input.uv.y);
    if (frac(seed*7.13) > .5) uv.x = 1-uv.x;
    float4 flame = flameSample(saturate(uv));
    float mask = dot(flame.rgb,float3(.2126,.7152,.0722))*flame.a;
    if (input.layer < .5) {
        // The compact white core fires first and dies before the outer gas.
        density *= lerp(.75,1,mask) * (1-smoothstep(.05,.55,phase));
    } else if (input.layer < 1.5) {
        float erosion = lerp(.025,.48,phase);
        density *= smoothstep(erosion,erosion+.16,mask+.09*turbulent) *
            lerp(.35,1,smoothstep(0,.20,phase)) * pow(saturate(1-phase),max(.1,flashAnimation.w));
    } else {
        density *= (1-smoothstep(.02,.30,phase));
    }
    density *= lerp(.85,1.15,frac(seed*19.31));
    if (input.layer < 1.5) {
        // Approximate the optical thickness of a luminous gas volume. Fade
        // grazing views of the curved sheets instead of outlining a solid cone.
        float3 normal = normalize(cross(ddx(input.localPosition), ddy(input.localPosition)));
        float3 view = normalize(cameraPosition - input.localPosition);
        density *= pow(saturate(abs(dot(normal, view))), .70);
    }
    if (input.layer > .5 && input.layer < 1.5) {
        density *= pow(saturate(sin(input.uv.x * 3.14159265)), .7);
    }
    float edge = saturate(density / max(fwidth(density), .008));
    density *= edge;
    float3 temperature = lerp(flashCoolColour, flashHotColour, smoothstep(.05,.98,heat));
    float layerBrightness = input.layer < .5 ? 1.8 : (input.layer < 1.5 ? 1 : .6);
    // Premultiplied emission with ONE/ONE blending: soft edges remain soft,
    // transparent triangles need no depth sorting, and there is no dark rim.
    return float4(temperature * density * flashParams.x * layerBrightness, 0);
}
