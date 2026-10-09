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
in vec4 vMask;  // r forest, g town, b beach/wet, a snow/rock bias
uniform sampler2D uNoise;
uniform float uCloudCover;
uniform vec3 uFlashPos;
uniform vec3 uFlashColor;
out vec4 finalColor;

float hash12(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

float cloudShadow(vec3 p) {
    // project onto the cloud layer (y = 2300) along the sun direction
    float t = (2300.0 - p.y) / max(uSunDir.y, 0.05);
    vec2 q = (p.xz + uSunDir.xz * t) / 9000.0 + vec2(uTime * 0.0025, uTime * 0.001);
    float n = texture(uNoise, q).r * 0.65 + texture(uNoise, q * 3.1).g * 0.35;
    float c = smoothstep(1.0 - uCloudCover - 0.02, 1.0 - uCloudCover + 0.22, n);
    return 1.0 - c * 0.72;
}

void main() {
    vec3 n = normalize(vNormal);
    // detail normal from noise
    vec2 dUv = vPos.xz / 37.0;
    float dx = texture(uNoise, dUv + vec2(0.01, 0.0)).g - texture(uNoise, dUv - vec2(0.01, 0.0)).g;
    float dz = texture(uNoise, dUv + vec2(0.0, 0.01)).g - texture(uNoise, dUv - vec2(0.0, 0.01)).g;
    n = normalize(n + vec3(-dx, 0.0, -dz) * 0.9);

    float slope = 1.0 - n.y;
    float macro = texture(uNoise, vPos.xz / 4100.0).r;
    float meso = texture(uNoise, vPos.xz / 610.0).g;
    float micro = texture(uNoise, vPos.xz / 23.0).b;

    vec3 grassDry = vec3(0.36, 0.33, 0.18);
    vec3 grassGreen = vec3(0.20, 0.26, 0.11);
    vec3 grass = mix(grassGreen, grassDry, smoothstep(0.35, 0.7, macro + (meso - 0.5) * 0.4));
    grass *= 0.82 + 0.36 * micro;
    vec3 forest = vec3(0.10, 0.15, 0.07) * (0.75 + 0.5 * meso);
    vec3 rock = mix(vec3(0.36, 0.34, 0.31), vec3(0.50, 0.46, 0.40), meso) * (0.8 + 0.35 * micro);
    vec3 sand = vec3(0.70, 0.63, 0.47) * (0.9 + 0.2 * micro);
    vec3 snow = vec3(0.92, 0.94, 0.98);
    vec3 town = mix(vec3(0.48, 0.45, 0.40), vec3(0.62, 0.58, 0.50), micro);

    vec3 albedo = grass;
    // agricultural patchwork on gentle lowland (warped grid of fields with hedgerows)
    {
        vec2 wp = vPos.xz + (texture(uNoise, vPos.xz / 3300.0).rg - 0.5) * 520.0;
        vec2 sz = vec2(230.0, 160.0);
        vec2 cell = floor(wp / sz);
        vec2 fr = fract(wp / sz);
        float h1 = hash12(cell), h2 = hash12(cell + 17.3);
        vec3 fc = vec3(0.24, 0.31, 0.12);
        if (h1 < 0.22) fc = vec3(0.56, 0.49, 0.27);        // ripe wheat
        else if (h1 < 0.40) fc = vec3(0.36, 0.29, 0.20);   // ploughed soil
        else if (h1 < 0.62) fc = vec3(0.27, 0.35, 0.13);   // pasture
        else if (h1 < 0.80) fc = vec3(0.19, 0.25, 0.10);   // dark crop
        else fc = vec3(0.44, 0.43, 0.24);                  // dry grass
        float ang = h2 * 3.14159;
        float furrow = 0.9 + 0.1 * sin(dot(vPos.xz, vec2(cos(ang), sin(ang))) * 0.9);
        fc *= furrow * (0.9 + 0.2 * micro);
        float edge = min(min(fr.x, 1.0 - fr.x) * sz.x, min(fr.y, 1.0 - fr.y) * sz.y);
        fc = mix(vec3(0.12, 0.16, 0.07), fc, smoothstep(2.0, 5.0, edge));
        float farm = smoothstep(0.10, 0.03, 1.0 - vNormal.y) * smoothstep(420.0, 260.0, vPos.y) * smoothstep(12.0, 30.0, vPos.y);
        farm *= smoothstep(0.38, 0.5, texture(uNoise, vPos.xz / 9000.0 + 0.37).g);
        albedo = mix(albedo, fc, farm * 0.9);
    }
    albedo = mix(albedo, forest, clamp(vMask.r, 0.0, 1.0));
    albedo = mix(albedo, town, clamp(vMask.g, 0.0, 1.0));
    float rockW = smoothstep(0.16, 0.34, slope + (meso - 0.5) * 0.12);
    albedo = mix(albedo, rock, rockW);
    float snowW = smoothstep(1250.0, 1500.0, vPos.y + meso * 220.0) * (1.0 - smoothstep(0.35, 0.6, slope));
    albedo = mix(albedo, snow, snowW);
    float beach = smoothstep(9.0, 2.0, vPos.y);
    albedo = mix(albedo, sand, beach);
    albedo = mix(albedo, albedo * 0.55, smoothstep(1.0, -2.0, vPos.y)); // wet below waterline

    float sh = cloudShadow(vPos);
    float ndl = max(dot(n, uSunDir), 0.0);
    vec3 light = uSunColor * ndl * sh + ambientLight(n) * (0.75 + 0.25 * sh);
    vec3 col = albedo * light;
    // explosion flash
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
    vec3 col = albedo * (uSunColor * ndl + ambientLight(n)) + uSunColor * spec;
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
in vec4 vColor;      // rgb = part tint multiplier, a = material id (1 = paint, 0.5 = dark metal, 0 = glass)
uniform vec3 uPaintA;
uniform vec3 uPaintB;
uniform float uCamo;        // 0 = uniform grey, 1 = digital splinter camouflage
uniform float uRoughness;
uniform float uHitFlash;
uniform float uDamage;
uniform vec3 uFlashPos;
uniform vec3 uFlashColor;
uniform sampler2D uNoise;
out vec4 finalColor;

float D_GGX(float NoH, float a) { float a2 = a * a; float d = NoH * NoH * (a2 - 1.0) + 1.0; return a2 / (3.14159 * d * d); }

void main() {
    vec3 n = normalize(vNormal);
    if (!gl_FrontFacing) n = -n;
    vec3 v = normalize(uCamPos - vPos);
    float mat = vColor.a;
    vec3 base;
    float metal, rough;
    if (mat > 0.75) {
        // paint
        vec3 paint = uPaintA;
        if (uCamo > 0.5) {
            vec2 c = floor(vObj.xz * vec2(0.9, 1.4) + floor(vObj.y * 2.0));
            float r = texture(uNoise, c / 61.0).r;
            float r2 = texture(uNoise, vObj.xz / 17.0).g;
            paint = mix(uPaintA, uPaintB, step(0.5, r * 0.7 + r2 * 0.5));
        } else {
            float m = texture(uNoise, vObj.xz / 9.0).r;
            paint = mix(uPaintA, uPaintB, smoothstep(0.45, 0.75, m) * 0.6);
        }
        base = paint * vColor.rgb;
        // panel lines
        vec3 pl = abs(fract(vObj * vec3(0.32, 0.9, 0.41) + vec3(0.13, 0.0, 0.27)) - 0.5);
        float line = smoothstep(0.488, 0.497, max(pl.x, pl.z));
        line *= step(0.45, texture(uNoise, floor(vObj.xz * vec2(0.32, 0.41)) / 37.0).b);
        base *= 1.0 - line * 0.16;
        // weathering
        base *= 0.88 + 0.24 * texture(uNoise, vObj.xz / 3.0).b;
        metal = 0.22; rough = uRoughness;
    } else if (mat > 0.25) {
        base = vColor.rgb; metal = 0.85; rough = 0.42;
    } else {
        base = vec3(0.04, 0.035, 0.02); metal = 0.0; rough = 0.06;
    }
    base *= (1.0 - uDamage * 0.6 * texture(uNoise, vObj.xz / 2.0).r);

    vec3 l = uSunDir;
    vec3 h = normalize(l + v);
    float NoL = max(dot(n, l), 0.0);
    float NoV = max(dot(n, v), 1e-3);
    float NoH = max(dot(n, h), 0.0);
    float a = rough * rough;
    vec3 F0 = mix(vec3(0.04), base, metal);
    vec3 F = F0 + (1.0 - F0) * pow(1.0 - max(dot(h, v), 0.0), 5.0);
    float spec = D_GGX(NoH, max(a, 0.02)) * 0.25 / max(NoL * NoV, 0.1);
    vec3 diffuse = base * (1.0 - metal) * NoL;
    vec3 col = (diffuse + F * spec * NoL) * uSunColor;
    // environment: sky above, darker ground below (horizon line reflects like real metal)
    vec3 r = reflect(-v, n);
    vec3 env = r.y > 0.0 ? skyColor(r, 0.0) : mix(uGroundHaze * 0.7, uGroundHaze * 0.4, clamp(-r.y * 3.0, 0.0, 1.0));
    float envLum = dot(env, vec3(0.3, 0.5, 0.2));
    env = mix(vec3(envLum), env, 0.55);   // desaturate reflections: paint is not a mirror
    vec3 Fe = F0 + (1.0 - F0) * pow(1.0 - NoV, 5.0) * (1.0 - rough);
    vec3 amb = ambientLight(n);
    amb = mix(vec3(dot(amb, vec3(0.33))), amb, 0.6);
    col += env * Fe * (mat < 0.25 ? 1.4 : 0.85) + base * amb * (1.0 - metal * 0.5) * 1.45;
    if (mat < 0.25) col += vec3(0.35, 0.25, 0.06) * pow(1.0 - NoV, 2.0) * 0.6; // gold canopy tint
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
    float a = smoothstep(0.0, 0.42, dens);
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
    vec3 col = albedo * (uSunColor * max(uSunDir.y, 0.0) + ambientLight(nrm));
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
