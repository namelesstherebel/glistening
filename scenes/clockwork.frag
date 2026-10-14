// @name Clockwork
// @desc nested gears, a pendulum and an orrery; the music clock turns the trains, bass swings the pendulum, mids open the orbits, highs shiver the balance wheel, downbeats engage the front gear
// @busy 0.5
// @bloom 0.7
// @palettes brass,solar,silver,ember
out vec4 fragColor;

// Every gear shares one module: pitch radius = M * teeth, so meshing gears have
// the same tooth size and sit (teeth i + teeth j) * M apart.
const float M = 0.011;
const vec2 LIGHT = vec2(-0.6, 0.8);  // key light from the top left

// Signed distance (screen units, approximate) to a spur gear: n teeth, turned by a, s spokes.
float gear(vec2 p, float n, float a, float s) {
    float R = M * n, r = length(p), ang = atan(p.y, p.x) - a;
    float d = r - R - 0.9 * M * clamp(cos(ang * n) * 2.5, -1.0, 1.0);
    float sector = (fract(ang * s / TAU + 0.5) - 0.5) * TAU / s;  // angle off the nearest spoke
    float hole = max(max(r - (R - 2.8 * M), 0.3 * R + 0.012 - r), 0.07 * R + 0.006 - abs(sector) * r);
    return max(max(d, -hole), 0.01 - r);  // spokes and rim, axle bore
}

// Angle of gear j (nj teeth) meshing with gear i (ni teeth, angle ai) from direction th:
// gear j turns at -ni/nj the rate, and puts a gap where gear i puts a tooth.
float meshed(float ai, float ni, float nj, float th) { return th + PI + (ni * (th - ai) - PI) / nj; }

float cover(float d) { return clamp(0.5 - d / max(fwidth(d), 1e-4), 0.0, 1.0); }

// Machined brass: bevelled edges catch the key light, faint turning rings, lit glow.
vec3 metal(vec2 q, float d, float tone, float glow) {
    vec2 g = vec2(dFdx(d), dFdy(d));
    float bevel = (1.0 - smoothstep(0.0, 0.009, -d)) * dot(g / max(length(g), 1e-6), normalize(LIGHT));
    vec3 base = mix(uPal[2], uPal[3], tone) * (0.62 + 0.06 * sin(length(q) * 900.0));
    return base * (1.0 - 0.45 * max(-bevel, 0.0)) + uPal[4] * (0.4 * max(bevel, 0.0) + glow);
}

void main() {
    vec2 uv = (gl_FragCoord.xy - 0.5 * uRes) / uRes.y;
    if (uVariant.x > 0.5) uv.x = -uv.x;  // mirrored movement for variety

    // The music clock turns the main train; each beat kicks it on a notch (the escapement).
    float a0 = 0.3 * uFlow + 0.12 * uBeat + uSeed * TAU;
    vec2 c0 = vec2(-0.38, -0.02);
    float n0 = 26.0, n1 = 12.0, n2 = 20.0, n3 = 9.0, n4 = 14.0;
    float t1 = 0.5, t2 = -0.55, t3 = 1.25, t4 = -2.2;
    vec2 c1 = c0 + M * (n0 + n1) * vec2(cos(t1), sin(t1));
    vec2 c2 = c1 + M * (n1 + n2) * vec2(cos(t2), sin(t2));
    vec2 c3 = c2 + M * (n2 + n3) * vec2(cos(t3), sin(t3));
    float a1 = meshed(a0, n0, n1, t1), a2 = meshed(a1, n1, n2, t2), a3 = meshed(a2, n2, n3, t3);
    // Downbeats slide the front gear into mesh; a drop keeps it engaged.
    float engage = max(smoothstep(0.0, 0.5, uDownbeat), uDrop);
    vec2 c4 = c0 + (M * (n0 + n4) + 0.09 * (1.0 - engage)) * vec2(cos(t4), sin(t4));
    float a4 = meshed(a0, n0, n4, t4);

    // Back plate: dark, engraved rings, two slow ghost wheels behind the movement.
    vec3 col = mix(uPal[0], uPal[1] * 0.55, 1.0 - smoothstep(0.0, 0.9, length(uv - vec2(0.0, 0.1))));
    col += uPal[1] * 0.08 * (1.0 - smoothstep(0.0, 0.3, abs(fract(length(uv - c0) * 18.0) - 0.5)));
    float back = min(gear(uv - vec2(0.3, -0.52), 40.0, -0.05 * uFlow, 8.0), gear(uv - vec2(-0.8, 0.48), 30.0, 0.07 * uFlow, 6.0));
    col = mix(col, uPal[1] * 0.9 + uPal[2] * 0.08, 0.8 * cover(back));

    // Main train with a soft drop shadow. Bass warms the big wheel, beats glint along the teeth.
    vec2 sh = uv + vec2(-0.012, 0.016);
    float ds = min(min(gear(sh - c0, n0, a0, 6.0), gear(sh - c1, n1, a1, 4.0)), min(gear(sh - c2, n2, a2, 5.0), gear(sh - c3, n3, a3, 3.0)));
    col *= 1.0 - 0.6 * (1.0 - smoothstep(-0.01, 0.03, ds));
    float d0 = gear(uv - c0, n0, a0, 6.0), d1 = gear(uv - c1, n1, a1, 4.0);
    float d2 = gear(uv - c2, n2, a2, 5.0), d3 = gear(uv - c3, n3, a3, 3.0);
    float tip = 0.08 * uBeat;
    col = mix(col, metal(uv - c0, d0, 0.45, 0.1 * uBass + tip * smoothstep(-0.02, 0.0, d0)), cover(d0));
    col = mix(col, metal(uv - c1, d1, 0.7, tip * smoothstep(-0.02, 0.0, d1)), cover(d1));
    col = mix(col, metal(uv - c2, d2, 0.55, tip * smoothstep(-0.02, 0.0, d2)), cover(d2));
    col = mix(col, metal(uv - c3, d3, 0.8, tip * smoothstep(-0.02, 0.0, d3)), cover(d3));
    for (int k = 0; k < 4; k++) {  // brass arbors with a jewel in each axle
        vec2 c = k == 0 ? c0 : k == 1 ? c1 : k == 2 ? c2 : c3;
        float r = length(uv - c);
        col = mix(col, uPal[3] * 0.8, cover(r - 0.018));
        col = mix(col, uPal[4] * (0.4 + 0.5 * uBeat), cover(r - 0.007));
    }

    // Orrery (mids open the orbits): planets on arms round a glowing sun.
    vec2 co = vec2(0.64, -0.24), q = uv - co;
    float open = 0.75 + 0.5 * uMid;
    col = mix(col, uPal[2] * 0.5, 0.6 * cover(abs(length(q) - 0.21 * open) - 0.004));
    for (int k = 0; k < 3; k++) {
        float fk = float(k), rk = (0.07 + 0.065 * fk) * open, ak = uFlow * 0.5 / (fk + 1.0) + fk * 2.1 + uSeed * 9.0;
        vec2 pk = rk * vec2(cos(ak), sin(ak));
        col += uPal[2] * 0.12 * cover(abs(length(q) - rk) - 0.0015);  // orbit
        vec2 e = q - pk * clamp(dot(q, pk) / dot(pk, pk), 0.0, 1.0);
        col = mix(col, uPal[3] * 0.7, cover(length(e) - 0.004));  // arm
        float pd = length(q - pk) - (0.022 - 0.004 * fk);
        col = mix(col, metal(q - pk, pd, 0.9, 0.15 * uMid), cover(pd));
    }
    col = mix(col, uPal[3] + uPal[4] * (0.3 + 0.6 * uMid), cover(length(q) - 0.035));

    // Balance wheel (highs shiver it) with its hairspring.
    vec2 cb = vec2(0.66, 0.3), b = uv - cb;
    float ab = (0.25 + 1.6 * uHigh) * sin(uTime * TAU * 2.0);
    b = rot(ab) * b;
    float rb = length(b), sb = (fract(atan(b.y, b.x) * 3.0 / TAU + 0.5) - 0.5) * TAU / 3.0;
    float wheel = min(abs(rb - 0.085) - 0.009, max(rb - 0.085, abs(sb) * rb - 0.005));
    float spring = (abs(fract(rb * 70.0 - atan(b.y, b.x) / TAU) - 0.5) - 0.12) / 70.0;  // Archimedean spiral
    col = mix(col, uPal[4] * (0.25 + 0.4 * uHigh), 0.7 * cover(max(spring, rb - 0.06)));
    col = mix(col, metal(b, wheel, 0.85, 0.15 * uHigh), cover(wheel));

    // Front gear, engaged on downbeats.
    float d4 = gear(uv - c4, n4, a4, 5.0);
    col = mix(col, metal(uv - c4, d4, 0.95, 0.2 * engage), cover(d4) * (0.35 + 0.65 * engage));

    // Pendulum (bass widens the swing), crossing centre on every beat.
    vec2 piv = vec2(0.1, 0.52);
    float swing = (0.08 + 0.22 * uBass) * sin(TAU * 2.0 * uBarPhase);
    vec2 dir = vec2(sin(swing), -cos(swing)), bob = piv + 0.74 * dir, w = uv - piv;
    float rod = length(w - dir * clamp(dot(w, dir), 0.0, 0.74)) - 0.005;
    col = mix(col, uPal[3] * 0.75, cover(rod));
    float bd = length(uv - bob) - 0.055;
    col = mix(col, metal(uv - bob, bd, 1.0, 0.12 * uBeat), cover(bd));

    col *= (0.4 + 0.6 * uIntensity) * max(0.0, 1.0 - 0.35 * dot(uv, uv));
    fragColor = vec4(col, 1.0);
}
