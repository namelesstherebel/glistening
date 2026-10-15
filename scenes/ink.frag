// @name Ink
// @desc luminous dye carried by a slow curl-noise current
// @busy 0.3
// @bloom 0.8
// @palettes indigo,abyss,orchid,ember
out vec4 fragColor;

float potential(vec2 p) {
    float t = uFlow * 0.06;
    return gnoise(p * 1.3 + vec2(t, -t * 0.7) + uSeed * 9.0) + (0.35 + 0.4 * uMid) * gnoise(p * 3.7 - vec2(t * 1.3, t) + 4.0);
}
vec2 curl(vec2 p) {
    const float e = 0.004;
    return vec2(potential(p + vec2(0, e)) - potential(p - vec2(0, e)), potential(p - vec2(e, 0)) - potential(p + vec2(e, 0))) / (2.0 * e);
}
vec3 backdrop(vec2 uv) {
    return uPal[0] * 0.35 + uPal[1] * 0.08 * smoothstep(-0.2, 1.0, uv.y);
}
vec2 emitter(float k, float flow) {
    float t = flow * (0.05 + 0.02 * k) + uSeed * 20.0 + k * 2.1;
    return vec2(0.55 * sin(t * 1.1 + k), 0.3 * sin(t * 0.8 + k * 3.0));
}
// Dye laid along the emitter's path since the last frame, so fast sources draw lines, not dots.
vec3 splat(vec2 p, float k, float r, vec3 col, float a) {
    vec2 b = emitter(k, uFlow), c = emitter(k, uFlow - uFlowDelta), ba = c - b;
    float h = clamp(dot(p - b, ba) / max(dot(ba, ba), 1e-8), 0.0, 1.0);
    vec2 d = p - b - ba * h;
    return col * a * exp(-dot(d, d) / (r * r));
}

void main() {
    vec2 uv = gl_FragCoord.xy / uRes;
    float asp = uRes.x / uRes.y;
    vec2 p = (uv - 0.5) * vec2(asp, 1.0);

    // Advect: trace back along the current; drops push outward from the centre.
    vec2 v = curl(p) * (0.035 + 0.08 * uMid + 0.05 * uEnergy) * (1.0 + 2.0 * uBeat);  // the water lurches on the kick
    v += normalize(p + 1e-4) * uDrop * 0.25 * exp(-dot(p, p) * 2.0);
    vec2 back = uv - v * uDt / vec2(asp, 1.0);
    vec3 dye = max(texture(uPrev, back).rgb - backdrop(back), 0.0);
    dye *= exp(-uDt * (0.6 + 0.4 * (1.0 - uEnergySlow)));  // ink lingers longer when the music is full

    // Three sources: bass puffs on beats, mids stream, highs draw thin fibres.
    float s = uDt * 30.0 * uIntensity;
    dye += splat(p, 0.0, 0.035 + 0.035 * uBass, uPal[3], s * (0.006 + 0.02 * uBass * uBass + 0.25 * uBeat * uBass + 0.3 * uDrop));
    dye += splat(p, 1.0, 0.025 + 0.015 * uMid, uPal[2] * 1.6, s * (0.008 + 0.03 * uMid));
    dye += splat(p, 2.0, 0.01, uPal[4], s * (0.006 + 0.12 * uHigh * uHigh));
    dye = min(dye, vec3(1.2));
    fragColor = vec4(backdrop(uv) + dye, 1.0);
}
