// Shared by down/post: the two scene slots and the dissolve between them.
uniform sampler2D uA, uB;  // A = incoming/current, B = outgoing
uniform float uMix, uSeed, uExtA, uExtB;
uniform vec2 uRes;
in vec2 vUv;

float h21(vec2 p) {
    vec3 q = fract(vec3(p.xyx) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p), u = f * f * (3.0 - 2.0 * f);
    return mix(mix(h21(i), h21(i + vec2(1, 0)), u.x), mix(h21(i + vec2(0, 1)), h21(i + vec2(1, 1)), u.x), u.y);
}
// External frames (projectM) are display-referred: undo the post tonemap so they survive it.
vec3 fromDisplay(vec3 c) { return -log(1.0 - pow(clamp(c, 0.0, 0.97), vec3(2.2))); }
vec3 slotA(vec2 uv) { vec3 c = texture(uA, uv).rgb; return uExtA > 0.5 ? fromDisplay(c) : c; }
vec3 slotB(vec2 uv) { vec3 c = texture(uB, uv).rgb; return uExtB > 0.5 ? fromDisplay(c) : c; }
// Organic dissolve: a slow noise field decides which pixels switch first.
float mixMask(vec2 uv) {
    if (uMix <= 0.0) return 0.0;
    if (uMix >= 1.0) return 1.0;
    vec2 p = uv * vec2(uRes.x / uRes.y, 1.0);
    float n = 0.65 * vnoise(p * 2.5 + uSeed * 40.0) + 0.35 * vnoise(p * 7.0 - uSeed * 17.0);
    // Warp progress toward each pixel's noise value. The warp stays monotone
    // and strictly inside (0, 1) throughout the dissolve.
    float t = uMix + (0.5 - n) * 2.0 * uMix * (1.0 - uMix);
    return smoothstep(0.0, 1.0, t);
}
vec3 scene(vec2 uv) {
    float m = mixMask(uv);
    vec3 a = slotA(uv);
    return m >= 1.0 ? a : mix(slotB(uv), a, m);
}
