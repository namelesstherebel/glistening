// @name Quasicrystal
// @desc interfering waves folded into rosettes that never repeat; it turns on every phrase
// @busy 0.45
// @bloom 0.75
// @palettes glacier,orchid,solar,silver
out vec4 fragColor;

// Sum of n plane waves at n-fold symmetry (5 or 7: never periodic); .x is the
// value, .yz its gradient.
vec3 field(vec2 p, float n, float rotA, float speed) {
    vec3 q = vec3(0.0);
    for (int k = 0; k < 9; k++) {
        if (float(k) >= n) break;
        float a = rotA + float(k) * PI / n;
        vec2 d = vec2(cos(a), sin(a));
        float ph = dot(p, d) + uFlow * speed * (1.0 + 0.13 * float(k)) * (mod(float(k), 2.0) * 2.0 - 1.0);
        q += vec3(cos(ph), -sin(ph) * d);
    }
    return q / n;
}

void main() {
    vec2 uv = (gl_FragCoord.xy - 0.5 * uRes) / uRes.y;
    float r2 = dot(uv, uv);
    float n = uVariant.z < 0.5 ? 5.0 : 7.0;
    // Every 16 beats the plate turns by one symmetry step, eased over the last beat.
    float block = floor(uBeats / 16.0), into = mod(uBeats, 16.0) + uBeatPhase;
    float rotA = uVariant.x * 6.28 + (block + smoothstep(15.0, 16.0, into)) * PI / n + uFlow * 0.004;
    float scale = 34.0 + 4.0 * sin(uFlow * 0.03 + uSeed * 6.0) - 4.0 * uBass - 4.0 * uDrop;
    vec2 p = uv * (1.0 + 0.35 * r2) * scale;  // a gentle lens

    vec3 f = field(p, n, rotA, 0.18 + 0.18 * uMid);
    float ripple = 0.25 * uBeat * exp(-r2 * 2.0);  // beat ripple from the centre
    float h = f.x + ripple * sin(length(uv) * 22.0 - uTime * 7.0);
    // Wrap the summed waves through a triangle wave: the folds are the crystal.
    float K = n * (0.55 + 0.12 * uBass + 0.1 * uDrop);
    float t = h * K, fw = fwidth(t);
    float tri = abs(fract(t + 0.5) * 2.0 - 1.0);
    tri = mix(tri, 0.5, smoothstep(0.35, 0.9, fw));  // fade to the mean where folds get finer than a pixel
    float fold = smoothstep(0.9, 1.0, tri) * (1.0 - smoothstep(0.3, 0.8, fw));  // creases
    float v = 0.5 + 0.5 * h;
    vec3 col = pal(0.05 + 0.6 * pow(tri, 1.5)) * (0.12 + 0.88 * tri);
    col += mix(uPal[3], uPal[4], 0.45) * fold * (0.3 + 0.6 * uBeat + 0.3 * uEnergySlow);
    col += mix(uPal[4], vec3(1.0), 0.4) * pow(smoothstep(0.8, 1.0, v), 8.0) * (0.4 + 2.0 * uHigh);  // nodes: every wave agrees
    col *= (1.0 - smoothstep(0.25, 1.15, length(uv * vec2(0.8, 1.0))));  // lens falloff
    col *= uIntensity * 0.6 + 0.4;
    fragColor = vec4(col, 1.0);
}
