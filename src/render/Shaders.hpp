// GLSL 330 shaders embedded as strings (no runtime file paths to get wrong).
// Shared atmosphere code keeps sky, fog, terrain, water and clouds consistent.
#pragma once

namespace sky::shaders {

// ---------------------------------------------------------------------------------------------
// Common: atmosphere / sky colour / height fog
// ---------------------------------------------------------------------------------------------
inline const char* kAtmosphere = R"GLSL(
uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform vec3 uZenith;
uniform vec3 uHorizon;
uniform vec3 uGroundHaze;
uniform vec3 uCamPos;
uniform float uFogDensity;
uniform float uFogFalloff;
uniform float uTime;

vec3 skyColor(vec3 d, float sunDisk) {
    float y = d.y;
    float h = pow(1.0 - clamp(y, 0.0, 1.0), 6.0);
    vec3 col = mix(uZenith, uHorizon, h);
    if (y < 0.0) col = mix(uHorizon, uGroundHaze, clamp(-y * 3.0, 0.0, 1.0));
    float mu = max(dot(d, uSunDir), 0.0);
    col += uSunColor * (pow(mu, 10.0) * 0.22 + pow(mu, 160.0) * 0.6) * (0.35 + 0.65 * h);
    col += uSunColor * smoothstep(0.99955, 0.99985, mu) * 28.0 * sunDisk;
    return col;
}

float fogAmount(vec3 worldPos) {
    vec3 v = worldPos - uCamPos;
    float dist = length(v);
    vec3 d = v / max(dist, 1e-3);
    float k = d.y * uFogFalloff;
    float integral = abs(k) > 1e-5 ? (1.0 - exp(-dist * k)) / k : dist;
    float density = uFogDensity * exp(-max(uCamPos.y, 0.0) * uFogFalloff);
    return 1.0 - exp(-density * integral);
}

vec3 applyFog(vec3 col, vec3 worldPos) {
    vec3 d = normalize(worldPos - uCamPos);
    vec3 fogCol = skyColor(normalize(vec3(d.x, max(d.y, 0.0) * 0.5, d.z)), 0.0);
    return mix(col, fogCol, clamp(fogAmount(worldPos), 0.0, 1.0));
}

uniform mat4 uLightVP;
uniform sampler2D uShadowMap;
uniform float uShadowOn;
float shadowAt(vec3 wp, float ndl) {
    if (uShadowOn < 0.5) return 1.0;
    vec4 lp = uLightVP * vec4(wp, 1.0);
    vec3 p = lp.xyz / lp.w * 0.5 + 0.5;
    if (p.x <= 0.0 || p.x >= 1.0 || p.y <= 0.0 || p.y >= 1.0 || p.z >= 1.0) return 1.0;
    float bias = 0.0008 + 0.0025 * (1.0 - clamp(ndl, 0.0, 1.0));
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    float occl = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            occl += (p.z - bias > texture(uShadowMap, p.xy + vec2(x, y) * texel).r) ? 1.0 : 0.0;
    occl /= 9.0;
    // fade out towards the edge of the shadow volume
    vec2 e = abs(p.xy - 0.5) * 2.0;
    occl *= 1.0 - smoothstep(0.8, 1.0, max(e.x, e.y));
    return 1.0 - 0.82 * occl;
}

vec3 ambientLight(vec3 n) {
    // hemispheric ambient: sky from above, warm ground bounce from below
    vec3 skyAmb = mix(uHorizon, uZenith, 0.5) * 0.9;
    vec3 grdAmb = uGroundHaze * 0.35;
    return mix(grdAmb, skyAmb, n.y * 0.5 + 0.5);
}
)GLSL";

// ---------------------------------------------------------------------------------------------
// Full-screen sky (drawn first). Reconstructs a view ray per pixel.
// ---------------------------------------------------------------------------------------------
inline const char* kSkyVS = R"GLSL(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
uniform mat4 mvp;
out vec2 vUv;
void main() { vUv = vertexTexCoord; gl_Position = mvp * vec4(vertexPosition, 1.0); }
)GLSL";

inline const char* kSkyFS = R"GLSL(
in vec2 vUv;
uniform mat4 uInvViewProj;
uniform vec2 uResolution;
uniform sampler2D uNoise;
uniform float uCirrus;
out vec4 finalColor;
void main() {
    vec2 ndc = (gl_FragCoord.xy / uResolution) * 2.0 - 1.0;
    vec4 p = uInvViewProj * vec4(ndc, 1.0, 1.0);
    vec3 dir = normalize(p.xyz / p.w - uCamPos);
    vec3 col = skyColor(dir, 1.0);
    // high cirrus streaks
    if (dir.y > 0.0) {
        vec2 uv = dir.xz / (dir.y + 0.08) * 0.08 + vec2(uTime * 0.0006, 0.0);
        float c = texture(uNoise, uv * vec2(0.6, 2.2)).g * texture(uNoise, uv * 0.35).r;
        c = smoothstep(0.18, 0.55, c) * smoothstep(0.0, 0.25, dir.y) * uCirrus;
        vec3 cc = mix(uHorizon * 1.1, uSunColor * 0.9 + vec3(0.3), pow(max(dot(dir, uSunDir), 0.0), 4.0));
        col = mix(col, cc, c * 0.55);
    }
    finalColor = vec4(col, 1.0);
}
)GLSL";

// ---------------------------------------------------------------------------------------------
// Terrain
// ---------------------------------------------------------------------------------------------
inline const char* kTerrainVS = R"GLSL(#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
out vec3 vPos;
out vec3 vNormal;
out vec4 vMask;
void main() {
    vec4 wp = matModel * vec4(vertexPosition, 1.0);
    vPos = wp.xyz;
    vNormal = vertexNormal;
    vMask = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)GLSL";

inline const char* kTerrainFS = R"GLSL(
in vec3 vPos;
in vec3 vNormal;
in vec4 vMask;  // r scrub, g town, b dry river bed
uniform sampler2D uNoise;
uniform sampler2D uTerrainData;   // RG normal.xz, B ambient occlusion, A baked sun shadow
uniform float uCloudCover;
uniform vec3 uFlashPos;
uniform vec3 uFlashColor;
out vec4 finalColor;

float hash12(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

float cloudShadow(vec3 p) {
    float t = (2300.0 - p.y) / max(uSunDir.y, 0.05);
    vec2 q = (p.xz + uSunDir.xz * t) / 9000.0 + vec2(uTime * 0.0025, uTime * 0.001);
    float n = texture(uNoise, q).r * 0.65 + texture(uNoise, q * 3.1).g * 0.35;
    float c = smoothstep(1.0 - uCloudCover - 0.02, 1.0 - uCloudCover + 0.22, n);
    return 1.0 - c * 0.68;
}

// small-scale height detail -> normal perturbation (rock grain, sand ripples)
vec2 detailGrad(vec2 uv, float s) {
    float e = 0.004;
    float hx = texture(uNoise, uv + vec2(e, 0.0)).g - texture(uNoise, uv - vec2(e, 0.0)).g;
    float hz = texture(uNoise, uv + vec2(0.0, e)).g - texture(uNoise, uv - vec2(0.0, e)).g;
    return vec2(hx, hz) * s;
}

void main() {
    float dist = length(uCamPos - vPos);
    // ---- normal: baked high-resolution terrain normal, falling back to the mesh outside the map
    vec2 tuv = vPos.xz / 32000.0 + 0.5;
    bool inMap = all(greaterThan(tuv, vec2(0.0))) && all(lessThan(tuv, vec2(1.0)));
    vec4 td = inMap ? texture(uTerrainData, tuv) : vec4(0.5, 0.5, 1.0, 1.0);
    vec3 n = normalize(vNormal);
    if (inMap) {
        vec2 nxz = td.rg * 2.0 - 1.0;
        n = normalize(vec3(nxz.x, sqrt(max(0.0, 1.0 - dot(nxz, nxz))), nxz.y));
    }
    float slope = 1.0 - n.y;
    float h = vPos.y;
    float near = 1.0 - smoothstep(600.0, 3500.0, dist);
    vec2 g1 = detailGrad(vPos.xz / 260.0, 1.6) + detailGrad(vPos.xz / 41.0, 0.9 * near);
    // craggy rock: extra high-frequency relief on steep ground, strongest close to the camera
    float steep = smoothstep(0.05, 0.25, slope);
    vec2 g2 = detailGrad(vPos.xz / 95.0 + vec2(0.37, 0.11), 2.6) + detailGrad(vPos.xz / 17.0, 1.6 * near);
    vec3 nd = normalize(n + vec3(-g1.x, 0.0, -g1.y) * (0.35 + slope) + vec3(-g2.x, 0.0, -g2.y) * steep * 1.1);

    // ---- material layers (arid mountains) ----
    float macro = texture(uNoise, vPos.xz / 5200.0).r;
    float meso = texture(uNoise, vPos.xz / 700.0).g;
    float micro = texture(uNoise, vPos.xz / 31.0).b;
    float grain = texture(uNoise, vPos.xz / 7.0).a;

    vec3 sandLight = vec3(0.47, 0.41, 0.32);
    vec3 sandDark = vec3(0.39, 0.32, 0.24);
    vec3 sand = mix(sandLight, sandDark, smoothstep(0.3, 0.75, macro + (meso - 0.5) * 0.5));
    sand *= 0.92 + 0.12 * micro;
    // wind-blown dune ripples on flat sand
    float ripple = sin(dot(vPos.xz, vec2(0.21, 0.13)) + meso * 9.0) * 0.5 + 0.5;
    sand *= 1.0 - 0.05 * ripple * near * (1.0 - slope * 3.0);
    vec3 gravel = mix(vec3(0.36, 0.32, 0.27), vec3(0.29, 0.26, 0.22), meso) * (0.85 + 0.25 * grain);
    // sedimentary rock strata: horizontal bands warped by noise
    float strata = sin(h * 0.085 + meso * 6.0 + macro * 4.0) * 0.5 + 0.5;
    float strata2 = sin(h * 0.31 + micro * 3.0) * 0.5 + 0.5;
    vec3 rockA = vec3(0.33, 0.30, 0.27), rockB = vec3(0.23, 0.21, 0.19), rockC = vec3(0.40, 0.37, 0.33);
    vec3 rock = mix(rockB, rockA, 0.35 + 0.65 * mix(meso, strata, 0.35));
    rock = mix(rock, rockC, smoothstep(0.7, 0.95, strata2) * 0.25);
    rock *= 0.80 + 0.32 * micro * grain + 0.1 * meso;
    vec3 highRock = vec3(0.43, 0.42, 0.40) * (0.85 + 0.2 * micro);
    vec3 snow = vec3(0.80, 0.82, 0.86);
    vec3 town = mix(vec3(0.36, 0.34, 0.30), vec3(0.45, 0.42, 0.37), micro);
    vec3 scrub = vec3(0.16, 0.16, 0.10) * (0.8 + 0.4 * meso);
    vec3 wadi = vec3(0.52, 0.48, 0.41) * (0.88 + 0.18 * grain);

    float ao = inMap ? td.b : 1.0;
    float cav = smoothstep(0.55, 0.95, ao);              // 0 in gullies/valleys, 1 on ridges/flats
    vec3 albedo = mix(gravel, sand, smoothstep(0.08, 0.02, slope) * 0.8 + 0.2);
    // desert pavement: darker varnished gravel patches and pale alluvial fan streaks on the flats
    float pave = smoothstep(0.55, 0.75, texture(uNoise, vPos.xz / 1900.0 + 0.31).g * 0.7 + meso * 0.3);
    albedo = mix(albedo, gravel * 0.82, pave * 0.65 * (1.0 - slope * 4.0));
    float fan = texture(uNoise, vec2(vPos.x / 2600.0, vPos.z / 330.0)).r;
    albedo = mix(albedo, sandLight * 1.08, smoothstep(0.6, 0.8, fan) * 0.35 * (1.0 - slope * 4.0));
    // sediment collects in concave ground: lighter, finer sand in the gully floors
    albedo = mix(albedo, sandLight * 1.03, (1.0 - cav) * smoothstep(0.30, 0.10, slope) * 0.6);
    float rockW = smoothstep(0.05, 0.17, slope + (meso - 0.5) * 0.08 - (1.0 - cav) * 0.05);
    albedo = mix(albedo, rock, rockW);
    albedo = mix(albedo, highRock, smoothstep(1050.0, 1500.0, h + meso * 200.0) * (0.6 + 0.4 * rockW));
    float snowW = smoothstep(1650.0, 1900.0, h + meso * 180.0) * (1.0 - smoothstep(0.25, 0.5, slope));
    albedo = mix(albedo, snow, snowW);
    // sparse scrub: dark dots where the vegetation mask allows it
    float dots = smoothstep(0.62, 0.70, texture(uNoise, vPos.xz / 23.0).r * 0.6 + texture(uNoise, vPos.xz / 9.0).g * 0.4);
    albedo = mix(albedo, scrub, clamp(vMask.r * 1.6, 0.0, 1.0) * dots * (1.0 - rockW));
    albedo = mix(albedo, wadi, clamp(vMask.b, 0.0, 1.0) * (1.0 - rockW));
    albedo = mix(albedo, town, clamp(vMask.g, 0.0, 1.0));
    // beach and wet sand at the waterline
    albedo = mix(albedo, vec3(0.52, 0.47, 0.37), smoothstep(9.0, 2.0, h));
    albedo = mix(albedo, albedo * 0.6, smoothstep(1.0, -2.0, h));

    // ---- lighting ----
    float sunShadow = inMap ? td.a : 1.0;
    float sh = cloudShadow(vPos) * sunShadow;
    float ndl = max(dot(nd, uSunDir), 0.0);
    sh *= shadowAt(vPos, ndl);
    float skyAO = mix(0.25, 1.0, ao * ao);
    vec3 amb = ambientLight(nd) * skyAO;
    // warm bounce light from sunlit sand fills the shadows (key to a desert look)
    vec3 bounce = vec3(0.55, 0.45, 0.33) * uSunColor * 0.05 * (1.0 - nd.y * 0.5) * skyAO;
    vec3 col = albedo * (uSunColor * ndl * sh * 1.05 + amb * 0.62 + bounce);
    // dusty specular sheen at grazing sun
    vec3 v = normalize(uCamPos - vPos);
    vec3 hv = normalize(uSunDir + v);
    col += uSunColor * pow(max(dot(nd, hv), 0.0), 24.0) * 0.025 * sh * (1.0 - rockW * 0.5);
    float fd = length(uFlashPos - vPos);
    col += albedo * uFlashColor * (1.0 / (1.0 + fd * fd * 0.0004));
    col = applyFog(col, vPos);
    finalColor = vec4(col, 1.0);
}
)GLSL";

// ---------------------------------------------------------------------------------------------
// Water
// ---------------------------------------------------------------------------------------------
inline const char* kWaterVS = R"GLSL(#version 330
in vec3 vertexPosition;
uniform mat4 mvp;
uniform mat4 matModel;
out vec3 vPos;
void main() {
    vPos = (matModel * vec4(vertexPosition, 1.0)).xyz;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)GLSL";

inline const char* kWaterFS = R"GLSL(
in vec3 vPos;
uniform sampler2D uNoise;
uniform sampler2D uDepthMap;   // sea-floor height over the map
uniform float uMapHalf;
out vec4 finalColor;
void main() {
    vec2 uv1 = vPos.xz / 210.0 + vec2(uTime * 0.012, uTime * 0.007);
    vec2 uv2 = vPos.xz / 77.0 - vec2(uTime * 0.017, -uTime * 0.011);
    vec2 uv3 = vPos.xz / 1900.0 + vec2(uTime * 0.002, 0.0);
    vec3 n1 = texture(uNoise, uv1).rgb * 2.0 - 1.0;
    vec3 n2 = texture(uNoise, uv2).gbr * 2.0 - 1.0;
    vec3 n3 = texture(uNoise, uv3).rgb * 2.0 - 1.0;
    vec3 n = normalize(vec3(n1.x + n2.x * 0.6 + n3.x * 0.8, 6.0, n1.y + n2.y * 0.6 + n3.y * 0.8));
    vec3 v = normalize(uCamPos - vPos);
    float dist = length(uCamPos - vPos);
    n = normalize(mix(n, vec3(0, 1, 0), smoothstep(2000.0, 14000.0, dist)));
    float fres = 0.02 + 0.98 * pow(1.0 - max(dot(n, v), 0.0), 5.0);
    vec3 r = reflect(-v, n);
    r.y = abs(r.y);
    vec3 refl = skyColor(r, 0.0);
    // depth-based colour: turquoise shallows near the coast
    vec2 duv = vPos.xz / (2.0 * uMapHalf) + 0.5;
    float floorH = texture(uDepthMap, duv).r * 200.0 - 100.0;
    float depth = clamp(-floorH, 0.0, 100.0);
    if (duv.x < 0.0 || duv.x > 1.0 || duv.y < 0.0 || duv.y > 1.0) depth = 100.0;
    vec3 deep = vec3(0.015, 0.075, 0.13);
    vec3 shallow = vec3(0.06, 0.32, 0.36);
    vec3 body = mix(shallow, deep, smoothstep(2.0, 45.0, depth));
    body *= (0.45 + 0.55 * max(uSunDir.y, 0.0)) * (uSunColor * 0.6 + 0.4);
    vec3 col = mix(body, refl, fres);
    vec3 h = normalize(uSunDir + v);
    float spec = pow(max(dot(n, h), 0.0), 900.0) * 25.0 + pow(max(dot(n, h), 0.0), 90.0) * 0.6;
    col += uSunColor * spec;
    // shoreline foam
    float foam = smoothstep(3.5, 0.0, depth) * smoothstep(0.45, 0.75, texture(uNoise, vPos.xz / 18.0 + uTime * 0.02).r);
    col = mix(col, vec3(0.9), foam * 0.6);
    col = applyFog(col, vPos);
    finalColor = vec4(col, 1.0);
}
)GLSL";

// ---------------------------------------------------------------------------------------------
// Generic lit surfaces (buildings, trees, vehicles, cockpit, missiles) — vertex colour * tint
// ---------------------------------------------------------------------------------------------
inline const char* kLitVS = R"GLSL(#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
out vec3 vPos;
out vec3 vNormal;
out vec4 vColor;
void main() {
    vPos = (matModel * vec4(vertexPosition, 1.0)).xyz;
    vNormal = normalize((matNormal * vec4(vertexNormal, 0.0)).xyz);
    vColor = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)GLSL";

inline const char* kLitFS = R"GLSL(
in vec3 vPos;
in vec3 vNormal;
in vec4 vColor;
uniform vec4 colDiffuse;
uniform float uEmissive;
uniform float uSpecular;
uniform float uFogOn;
uniform vec3 uFlashPos;
uniform vec3 uFlashColor;
out vec4 finalColor;
void main() {
    vec3 n = normalize(vNormal);
    if (!gl_FrontFacing) n = -n;
    vec3 albedo = vColor.rgb * colDiffuse.rgb;
    vec3 v = normalize(uCamPos - vPos);
    float ndl = max(dot(n, uSunDir), 0.0);
    vec3 h = normalize(uSunDir + v);
    float spec = pow(max(dot(n, h), 0.0), 40.0) * uSpecular;
    float shd = shadowAt(vPos, ndl);
    vec3 col = albedo * (uSunColor * ndl * shd + ambientLight(n)) + uSunColor * spec * shd;
    float fd = length(uFlashPos - vPos);
    col += albedo * uFlashColor * (1.0 / (1.0 + fd * fd * 0.0004));
    col = mix(col, albedo * 3.0, uEmissive);
    if (uFogOn > 0.5) col = applyFog(col, vPos);
    finalColor = vec4(col, vColor.a * colDiffuse.a);
}
)GLSL";

// ---------------------------------------------------------------------------------------------
// Aircraft: metallic PBR-lite with sky reflections, panel lines and camouflage.
// ---------------------------------------------------------------------------------------------
inline const char* kJetVS = R"GLSL(#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
out vec3 vPos;
out vec3 vObj;
out vec3 vNormal;
out vec4 vColor;
void main() {
    vPos = (matModel * vec4(vertexPosition, 1.0)).xyz;
    vObj = vertexPosition;
    vNormal = normalize((matNormal * vec4(vertexNormal, 0.0)).xyz);
    vColor = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)GLSL";

inline const char* kJetFS = R"GLSL(
in vec3 vPos;
in vec3 vObj;
in vec3 vNormal;
in vec4 vColor;      // rgb = part tint x baked ambient occlusion, a = material id (1 paint, 0.5 metal, 0 glass)
uniform vec3 uPaintA;
uniform vec3 uPaintB;
uniform float uCamo;        // 0 = two-tone grey, 1 = digital splinter camouflage
uniform float uRoughness;
uniform float uHitFlash;
uniform float uDamage;
uniform vec3 uFlashPos;
uniform vec3 uFlashColor;
uniform sampler2D uNoise;
out vec4 finalColor;

float D_GGX(float NoH, float a) { float a2 = a * a; float d = NoH * NoH * (a2 - 1.0) + 1.0; return a2 / (3.14159 * d * d); }
float V_SmithJoint(float NoV, float NoL, float a) {
    float a2 = a * a;
    float gv = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float gl = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-4);
}
vec3 envLight(vec3 r, float rough) {
    // sky above, sunlit desert below; rough surfaces see a blurred, greyer environment
    vec3 sky = skyColor(normalize(vec3(r.x, max(r.y, 0.02), r.z)), 0.0);
    vec3 ground = mix(uGroundHaze * 0.55, uGroundHaze * 0.32, clamp(-r.y * 2.5, 0.0, 1.0));
    vec3 env = r.y > 0.0 ? sky : ground;
    float horizon = smoothstep(-0.08, 0.06, r.y);
    env = mix(ground, sky, horizon);
    float l = dot(env, vec3(0.3, 0.5, 0.2));
    return mix(env, vec3(l) * 0.9, rough * 0.6);
}

void main() {
    vec3 n = normalize(vNormal);
    if (!gl_FrontFacing) n = -n;
    vec3 v = normalize(uCamPos - vPos);
    float mat = vColor.a;
    float ao = clamp(max(vColor.r, max(vColor.g, vColor.b)), 0.0, 1.0);
    vec3 base;
    float metal, rough, coat;
    if (mat > 0.75) {
        vec3 paint = uPaintA;
        if (uCamo > 0.5) {
            vec2 c = floor(vObj.xz * vec2(0.9, 1.4) + floor(vObj.y * 2.0));
            float r = texture(uNoise, c / 61.0).r;
            float r2 = texture(uNoise, vObj.xz / 17.0).g;
            paint = mix(uPaintA, uPaintB, step(0.5, r * 0.7 + r2 * 0.5));
        } else {
            // soft two-tone pattern with large, smooth blotches
            float m = texture(uNoise, vObj.xz / 11.0 + vObj.y * 0.05).r;
            paint = mix(uPaintA, uPaintB, smoothstep(0.42, 0.62, m) * 0.85);
        }
        base = paint * vColor.rgb;
        // panel lines and fastener rows (thin, irregular)
        vec3 pl = abs(fract(vObj * vec3(0.32, 0.9, 0.41) + vec3(0.13, 0.0, 0.27)) - 0.5);
        float line = smoothstep(0.490, 0.4985, max(pl.x, pl.z));
        line *= step(0.45, texture(uNoise, floor(vObj.xz * vec2(0.32, 0.41)) / 37.0).b);
        base *= 1.0 - line * 0.22;
        // subtle RAM-coating mottling and wear
        float mott = texture(uNoise, vObj.xz / 1.7 + vObj.y).b;
        base *= 0.93 + 0.12 * mott;
        // exhaust soot towards the tail
        base *= mix(1.0, 0.72, smoothstep(-6.0, -9.5, vObj.x));
        metal = 0.12; rough = uRoughness * (0.85 + 0.3 * mott); coat = 0.35;
    } else if (mat > 0.25) {
        base = vColor.rgb; metal = 0.9; rough = 0.38; coat = 0.0;
        // heat tint on nozzles
        base *= mix(vec3(1.0), vec3(1.05, 0.9, 1.15), smoothstep(-8.0, -10.0, vObj.x));
    } else {
        base = vec3(0.03, 0.028, 0.02); metal = 0.0; rough = 0.04; coat = 1.0;
    }
    base *= (1.0 - uDamage * 0.6 * texture(uNoise, vObj.xz / 2.0).r);

    vec3 l = uSunDir;
    vec3 h = normalize(l + v);
    float NoL = max(dot(n, l), 0.0);
    float NoV = max(dot(n, v), 1e-3);
    float NoH = max(dot(n, h), 0.0);
    float VoH = max(dot(v, h), 0.0);
    float a = max(rough * rough, 0.02);
    vec3 F0 = mix(vec3(0.04), base, metal);
    vec3 F = F0 + (1.0 - F0) * pow(1.0 - VoH, 5.0);
    vec3 specBase = D_GGX(NoH, a) * V_SmithJoint(NoV, NoL, a) * F;
    // clear-coat: a second, sharp lobe on top of the paint
    float Fc = 0.04 + 0.96 * pow(1.0 - VoH, 5.0);
    float specCoat = D_GGX(NoH, 0.025) * V_SmithJoint(NoV, NoL, 0.025) * Fc * coat;
    vec3 diffuse = base * (1.0 - metal) / 3.14159 * 3.14159;
    float shd = shadowAt(vPos, NoL);
    vec3 col = (diffuse + specBase + specCoat) * NoL * uSunColor * shd;
    // image-based lighting approximation
    vec3 r = reflect(-v, n);
    vec3 Fe = F0 + (max(vec3(1.0 - rough), F0) - F0) * pow(1.0 - NoV, 5.0);
    vec3 amb = ambientLight(n);
    amb = mix(vec3(dot(amb, vec3(0.33))), amb, 0.65);
    float specOcc = clamp(pow(NoV + ao, 2.0) - 1.0 + ao, 0.0, 1.0);
    col += envLight(r, rough) * Fe * specOcc * (mat < 0.25 ? 1.3 : 0.7);
    col += envLight(r, 0.0) * (0.04 + 0.96 * pow(1.0 - NoV, 5.0)) * coat * 0.35 * specOcc;
    col += base * (1.0 - metal) * amb * ao * 0.85;
    if (mat < 0.25) col += vec3(0.35, 0.25, 0.06) * pow(1.0 - NoV, 2.0) * 0.5;  // gold canopy coating
    float fd = length(uFlashPos - vPos);
    col += base * uFlashColor * (1.0 / (1.0 + fd * fd * 0.0004));
    col += vec3(1.0, 0.25, 0.1) * uHitFlash;
    col = applyFog(col, vPos);
    finalColor = vec4(col, 1.0);
}
)GLSL";

// ---------------------------------------------------------------------------------------------
// Afterburner / engine glow (additive)
// ---------------------------------------------------------------------------------------------
inline const char* kFlameFS = R"GLSL(
in vec3 vPos;
in vec3 vObj;
in vec3 vNormal;
in vec4 vColor;
uniform float uThrottle;   // 0..1 mil power
uniform float uAb;         // 0..1 afterburner
uniform sampler2D uNoise;
out vec4 finalColor;
void main() {
    // vObj.x runs from 0 at the nozzle to -1 at the tail of the plume (normalised mesh)
    float t = clamp(-vObj.x, 0.0, 1.0);
    vec3 v = normalize(uCamPos - vPos);
    float rim = abs(dot(normalize(vNormal), v));
    float core = pow(rim, 1.5);
    float flick = 0.8 + 0.4 * texture(uNoise, vec2(t * 0.6 - uTime * 3.7, vObj.y * 0.4 + uTime * 1.3)).r;
    float diamonds = 0.65 + 0.35 * pow(abs(sin(t * 26.0)), 6.0) * (1.0 - t);
    vec3 hot = vec3(1.0, 0.85, 0.55);
    vec3 mid = vec3(1.0, 0.45, 0.12);
    vec3 cool = vec3(0.45, 0.35, 0.9);
    vec3 col = mix(hot, mid, smoothstep(0.0, 0.5, t));
    col = mix(col, cool, smoothstep(0.55, 1.0, t) * 0.5);
    float intensity = (uAb * 3.2 + uThrottle * 0.35) * core * flick * diamonds * (1.0 - smoothstep(0.35, 1.0, t)) ;
    finalColor = vec4(col * intensity * vColor.rgb, 1.0);
}
)GLSL";

// ---------------------------------------------------------------------------------------------
// Cloud layer (large plane following the camera)
// ---------------------------------------------------------------------------------------------
inline const char* kCloudFS = R"GLSL(
in vec3 vPos;
uniform sampler2D uNoise;
uniform float uCloudCover;
uniform float uLayerY;
uniform float uLayerFade;
uniform float uLayerShift;
out vec4 finalColor;
float density(vec2 xz) {
    vec2 q = xz / 9000.0 + vec2(uTime * 0.0025, uTime * 0.001);
    float n = texture(uNoise, q).r * 0.65 + texture(uNoise, q * 3.1).g * 0.35;
    float det = texture(uNoise, xz / 700.0 + uTime * 0.004 + uLayerShift).b;
    float cov = uCloudCover - uLayerShift * 4.0;
    float base = smoothstep(1.0 - cov - 0.02, 1.0 - cov + 0.22, n);
    return clamp(base * (0.75 + 0.5 * det) - (1.0 - base) * det * 0.3, 0.0, 1.0);
}
void main() {
    float dens = density(vPos.xz);
    if (dens < 0.01) discard;
    // fake surface normal from the density gradient -> billowy relief
    float e = 60.0;
    float dx = density(vPos.xz + vec2(e, 0.0)) - density(vPos.xz - vec2(e, 0.0));
    float dz = density(vPos.xz + vec2(0.0, e)) - density(vPos.xz - vec2(0.0, e));
    bool above = uCamPos.y > uLayerY;
    vec3 N = normalize(vec3(-dx * 2.2, above ? 1.0 : -1.0, -dz * 2.2));
    vec3 v = normalize(vPos - uCamPos);
    float towardSun = pow(max(dot(v, uSunDir), 0.0), 6.0);
    float lam = clamp(dot(N, uSunDir) * 0.6 + 0.55, 0.0, 1.2);
    vec3 lit = uSunColor * (0.55 + 0.45 * lam) + ambientLight(vec3(0, 1, 0)) * 0.9;
    vec3 shade = ambientLight(vec3(0, -1, 0)) * 1.5 + uSunColor * 0.10;
    vec3 col;
    if (above) col = lit * (0.82 + 0.18 * dens);
    else col = mix(shade * 1.15, shade * 0.6, smoothstep(0.2, 1.0, dens)) + uSunColor * 0.08 * lam;
    // silver lining at thin edges
    col += uSunColor * towardSun * (1.0 - dens) * 0.7;
    float a = smoothstep(0.05, 0.6, dens) * (above ? 1.0 : 0.72);
    float dist = length(vPos.xz - uCamPos.xz);
    a *= uLayerFade * (1.0 - smoothstep(30000.0, 42000.0, dist));
    col = applyFog(col, vPos);
    finalColor = vec4(col, a);
}
)GLSL";

// ---------------------------------------------------------------------------------------------
// Runway markings (procedural)
// ---------------------------------------------------------------------------------------------
inline const char* kRunwayVS = R"GLSL(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
uniform mat4 mvp;
uniform mat4 matModel;
out vec3 vPos;
out vec2 vUv;
void main() {
    vPos = (matModel * vec4(vertexPosition, 1.0)).xyz;
    vUv = vertexTexCoord;   // x: metres along (0..L), y: metres across (-W/2..W/2)
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)GLSL";

inline const char* kRunwayFS = R"GLSL(
in vec3 vPos;
in vec2 vUv;
uniform sampler2D uNoise;
uniform float uLength;
uniform float uWidth;
uniform float uKind; // 0 runway, 1 taxiway/apron
out vec4 finalColor;
float box(float x, float a, float b) { return step(a, x) * step(x, b); }
void main() {
    float along = vUv.x, across = vUv.y;
    float n = texture(uNoise, vPos.xz / 6.0).b;
    float n2 = texture(uNoise, vPos.xz / 45.0).r;
    vec3 asphalt = vec3(0.16, 0.16, 0.17) * (0.82 + 0.3 * n) * (0.9 + 0.2 * n2);
    if (uKind > 0.5) asphalt = vec3(0.30, 0.30, 0.29) * (0.85 + 0.25 * n);
    float paint = 0.0;
    if (uKind < 0.5) {
        float ac = abs(across);
        float L = uLength;
        float endDist = min(along, L - along);
        // centreline dashes
        paint += box(ac, 0.0, 0.45) * step(mod(along, 50.0), 30.0) * step(80.0, endDist);
        // edge stripes
        paint += box(ac, uWidth * 0.5 - 1.6, uWidth * 0.5 - 0.7);
        // threshold piano keys
        float key = step(mod(ac + 0.9, 3.6), 1.8) * box(ac, 2.5, uWidth * 0.5 - 2.5);
        paint += key * box(endDist, 6.0, 36.0);
        // aiming point blocks
        paint += box(ac, 5.0, 10.0) * box(endDist, 300.0, 360.0);
        // touchdown zone bars
        paint += box(ac, 4.0, 6.5) * step(mod(endDist, 150.0), 22.0) * box(endDist, 140.0, 760.0);
        // tyre marks
        float tyre = box(ac, 2.0, 8.0) * box(endDist, 250.0, 900.0) * smoothstep(0.35, 0.7, texture(uNoise, vec2(along / 140.0, ac / 9.0)).r);
        asphalt = mix(asphalt, vec3(0.05), tyre * 0.6);
    } else {
        paint += box(abs(across), 0.0, 0.25) * 0.8;
        asphalt = mix(asphalt, asphalt * 0.9, 0.0);
    }
    paint = clamp(paint, 0.0, 1.0) * (0.75 + 0.25 * n);
    vec3 albedo = mix(asphalt, uKind > 0.5 ? vec3(0.85, 0.7, 0.15) : vec3(0.86), paint);
    vec3 nrm = vec3(0, 1, 0);
    vec3 col = albedo * (uSunColor * max(uSunDir.y, 0.0) * shadowAt(vPos, uSunDir.y) + ambientLight(nrm));
    vec3 v = normalize(uCamPos - vPos);
    vec3 h = normalize(uSunDir + v);
    col += uSunColor * pow(max(h.y, 0.0), 60.0) * 0.08;
    col = applyFog(col, vPos);
    finalColor = vec4(col, 1.0);
}
)GLSL";

// ---------------------------------------------------------------------------------------------
// Post-processing
// ---------------------------------------------------------------------------------------------
inline const char* kPostVS = R"GLSL(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 mvp;
out vec2 fragTexCoord;
void main() { fragTexCoord = vertexTexCoord; gl_Position = mvp * vec4(vertexPosition, 1.0); }
)GLSL";

inline const char* kBrightFS = R"GLSL(#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform float uThreshold;
out vec4 finalColor;
void main() {
    vec2 px = 1.0 / vec2(textureSize(texture0, 0));
    vec3 c = vec3(0.0);
    c += texture(texture0, fragTexCoord + px * vec2(-1, -1)).rgb;
    c += texture(texture0, fragTexCoord + px * vec2(1, -1)).rgb;
    c += texture(texture0, fragTexCoord + px * vec2(-1, 1)).rgb;
    c += texture(texture0, fragTexCoord + px * vec2(1, 1)).rgb;
    c *= 0.25;
    float l = max(max(c.r, c.g), c.b);
    float k = max(l - uThreshold, 0.0) / max(l, 1e-4);
    finalColor = vec4(min(c * k, vec3(40.0)), 1.0);
}
)GLSL";

inline const char* kBlurFS = R"GLSL(#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform vec2 uDir;
out vec4 finalColor;
void main() {
    vec2 px = uDir / vec2(textureSize(texture0, 0));
    vec3 c = texture(texture0, fragTexCoord).rgb * 0.2270270270;
    c += texture(texture0, fragTexCoord + px * 1.3846153846).rgb * 0.3162162162;
    c += texture(texture0, fragTexCoord - px * 1.3846153846).rgb * 0.3162162162;
    c += texture(texture0, fragTexCoord + px * 3.2307692308).rgb * 0.0702702703;
    c += texture(texture0, fragTexCoord - px * 3.2307692308).rgb * 0.0702702703;
    finalColor = vec4(c, 1.0);
}
)GLSL";

inline const char* kCompositeFS = R"GLSL(#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;     // HDR scene
uniform sampler2D uBloom1;
uniform sampler2D uBloom2;
uniform float uExposure;
uniform float uBloomStrength;
uniform float uVignette;
uniform float uBlackout;        // 0..1 grey/black-out
uniform float uRedout;
uniform float uDamage;          // red damage pulse
uniform float uTime;
uniform float uGrain;
uniform float uSpeedFx;
out vec4 finalColor;

vec3 aces(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }

vec3 tone(vec2 uv) { return aces(texture(texture0, uv).rgb * uExposure); }
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
    vec2 uv = fragTexCoord;
    vec2 cc = uv - 0.5;
    float r2 = dot(cc, cc);
    // FXAA-style edge smoothing on the tone-mapped image (the HDR target has no MSAA)
    vec2 px = 1.0 / vec2(textureSize(texture0, 0));
    vec3 cM = tone(uv);
    vec3 cNW = tone(uv + vec2(-1.0, -1.0) * px), cNE = tone(uv + vec2(1.0, -1.0) * px);
    vec3 cSW = tone(uv + vec2(-1.0, 1.0) * px), cSE = tone(uv + vec2(1.0, 1.0) * px);
    float lM = luma(cM), lNW = luma(cNW), lNE = luma(cNE), lSW = luma(cSW), lSE = luma(cSE);
    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
    vec3 col = cM;
    if (lMax - lMin > max(0.04, lMax * 0.12)) {
        vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), ((lNW + lSW) - (lNE + lSE)));
        float red = max((lNW + lNE + lSW + lSE) * 0.03125, 1.0 / 128.0);
        float rcp = 1.0 / (min(abs(dir.x), abs(dir.y)) + red);
        dir = clamp(dir * rcp, vec2(-8.0), vec2(8.0)) * px;
        vec3 a = 0.5 * (tone(uv + dir * (1.0 / 3.0 - 0.5)) + tone(uv + dir * (2.0 / 3.0 - 0.5)));
        vec3 b = a * 0.5 + 0.25 * (tone(uv + dir * -0.5) + tone(uv + dir * 0.5));
        float lb = luma(b);
        col = (lb < lMin || lb > lMax) ? a : b;
    }
    vec3 bloom = texture(uBloom1, uv).rgb * 0.6 + texture(uBloom2, uv).rgb * 0.8;
    col += aces(bloom * uBloomStrength * uExposure) * 0.9;
    col = clamp(col, 0.0, 1.0);
    // gentle filmic grade: lift shadows to teal, warm highlights
    float l = luma(col);
    col = mix(col, col * vec3(0.92, 1.0, 1.06), (1.0 - l) * 0.25);
    col = mix(col, col * vec3(1.05, 1.0, 0.94), l * 0.15);
    col = pow(col, vec3(1.0 / 1.05));
    // vignette
    col *= 1.0 - uVignette * smoothstep(0.15, 0.75, r2 * 1.6);
    // G effects
    float tunnel = smoothstep(0.05, 0.5, r2 * 2.2);
    col = mix(col, vec3(l) * 0.6, uBlackout * 0.7);
    col *= 1.0 - uBlackout * (0.25 + 0.75 * tunnel);
    col = mix(col, vec3(0.6, 0.02, 0.02) * l * 2.0, uRedout * (0.4 + 0.6 * tunnel));
    col = mix(col, vec3(0.75, 0.02, 0.0), uDamage * smoothstep(0.2, 0.8, r2 * 2.2) * 0.45);
    col += (hash(uv * 1000.0 + uTime) - 0.5) * uGrain;
    finalColor = vec4(col, 1.0);
}
)GLSL";

}  // namespace sky::shaders
