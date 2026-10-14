// Prepended to every scene stage. Colours are linear; post tonemaps.
uniform vec2 uRes;            // render size, pixels
uniform float uTime;          // seconds since this scene appeared
uniform float uDt;            // seconds since the previous frame
uniform float uFlow;          // music clock: advances faster with energy, bass and beats
uniform float uBass, uMid, uHigh, uEnergy, uEnergySlow;  // 0..1, eased
uniform float uBeat, uDownbeat, uDrop;                   // accents: quick rise, smooth fall
uniform float uBeatPhase, uBarPhase, uBpm, uConf, uBeats;
uniform float uIntensity;     // user effect intensity (1 = default)
uniform float uQuality;       // 0.25..1: scale loop counts with it
uniform float uSeed;          // 0..1, new each time the scene appears
uniform vec4 uVariant;        // four more 0..1 variant parameters
uniform vec3 uPal[5];         // deep, shadow, mid, accent, highlight
uniform sampler2D uPrev;      // this scene's previous frame (feedback)
uniform sampler2D uSpectrum;  // 64 log bands, 30 Hz .. 16 kHz
uniform sampler2D uWave;      // 1024 mono samples, -1..1, newest last
uniform sampler2D uHistory;   // 64 bands x 96 frames of spectrum
uniform float uHistRow;       // newest history row
uniform float uHistPos;       // rows pushed so far (mod 100000): scroll position of the history

#define PI 3.14159265
#define TAU 6.28318531

float spec(float x) { return texture(uSpectrum, vec2(clamp(x, 0.0, 1.0) * (63.0 / 64.0) + 0.5 / 64.0, 0.5)).r; }
float wave(float x) { return texture(uWave, vec2(clamp(x, 0.0, 1.0) * (1023.0 / 1024.0) + 0.5 / 1024.0, 0.5)).r; }
// age 0 = now, 1 = ~3 s ago
float hist(float x, float age) {
    float row = uHistRow - clamp(age, 0.0, 1.0) * 94.0;
    return texture(uHistory, vec2(clamp(x, 0.0, 1.0) * (63.0 / 64.0) + 0.5 / 64.0, (row + 0.5) / 96.0)).r;
}

vec3 pal(float t) {
    t = clamp(t, 0.0, 1.0) * 4.0;
    int i = int(min(floor(t), 3.0));
    float f = t - float(i);
    return mix(uPal[i], uPal[i + 1], f * f * (3.0 - 2.0 * f));
}

mat2 rot(float a) { float c = cos(a), s = sin(a); return mat2(c, s, -s, c); }

float hash11(float p) { p = fract(p * 0.1031); p *= p + 33.33; p *= p + p; return fract(p); }
float hash12(vec2 p) { vec3 q = fract(vec3(p.xyx) * 0.1031); q += dot(q, q.yzx + 33.33); return fract((q.x + q.y) * q.z); }
vec2 hash22(vec2 p) { vec3 q = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973)); q += dot(q, q.yzx + 33.33); return fract((q.xx + q.yz) * q.zy); }
vec3 hash33(vec3 p) { p = fract(p * vec3(0.1031, 0.1030, 0.0973)); p += dot(p, p.yxz + 33.33); return fract((p.xxy + p.yxx) * p.zyx); }

// Gradient noise, -1..1-ish.
float gnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p), u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float a = dot(hash22(i) * 2.0 - 1.0, f), b = dot(hash22(i + vec2(1, 0)) * 2.0 - 1.0, f - vec2(1, 0));
    float c = dot(hash22(i + vec2(0, 1)) * 2.0 - 1.0, f - vec2(0, 1)), d = dot(hash22(i + vec2(1, 1)) * 2.0 - 1.0, f - vec2(1, 1));
    return 1.4 * mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}
float gnoise(vec3 p) {
    vec3 i = floor(p), f = fract(p), u = f * f * (3.0 - 2.0 * f);
    #define G(o) dot(hash33(i + o) * 2.0 - 1.0, f - o)
    return 1.2 * mix(mix(mix(G(vec3(0, 0, 0)), G(vec3(1, 0, 0)), u.x), mix(G(vec3(0, 1, 0)), G(vec3(1, 1, 0)), u.x), u.y),
                     mix(mix(G(vec3(0, 0, 1)), G(vec3(1, 0, 1)), u.x), mix(G(vec3(0, 1, 1)), G(vec3(1, 1, 1)), u.x), u.y), u.z);
    #undef G
}
float fbm(vec2 p, int oct) {
    float s = 0.0, a = 0.5;
    mat2 m = mat2(1.6, 1.2, -1.2, 1.6);
    for (int i = 0; i < 8; i++) {
        if (i >= oct) break;
        s += a * gnoise(p);
        p = m * p + 3.1;
        a *= 0.5;
    }
    return s;
}

#ifdef VERTEX
out vec2 vLocal;
out float vHalf;
out vec4 vColor;
uniform float uCount;  // particles drawn this frame
// Gaussian capsule from a to b (clip space), sigma in pixels. Thin sprites keep their energy.
void sprite(vec2 a, vec2 b, float sigma, vec3 color, float alpha) {
    const vec2 corner[6] = vec2[](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));
    if (sigma < 0.8) { alpha *= (sigma * sigma) / 0.64; sigma = 0.8; }
    vec2 A = (a * 0.5 + 0.5) * uRes, B = (b * 0.5 + 0.5) * uRes, d = B - A;
    float len = length(d);
    vec2 t = len > 1e-3 ? d / len : vec2(1, 0), n = vec2(-t.y, t.x), c = corner[gl_VertexID];
    vec2 p = 0.5 * (A + B) + t * c.x * (0.5 * len + 2.5 * sigma) + n * c.y * 2.5 * sigma;
    vLocal = c * vec2(0.5 * len / sigma + 2.5, 2.5);
    vHalf = 0.5 * len / sigma;
    vColor = vec4(color, alpha);
    gl_Position = vec4(p / uRes * 2.0 - 1.0, 0.0, 1.0);
}
void hide() { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); vColor = vec4(0.0); vLocal = vec2(0.0); vHalf = 0.0; }
#else
in vec2 vUv;
#endif
