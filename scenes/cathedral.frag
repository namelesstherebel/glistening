// @name Laser Cathedral
// @desc light fans through haze under towering arches; bass widens the beams, mids steer them, highs split them, drops reveal the chamber
// @busy 0.8
// @bloom 1.0
// @palettes orchid,neon,glacier,ember
out vec4 fragColor;

const float F = 0.55;     // focal length (screen units per world unit at depth 1)
const float CAM = 1.0;    // eye height above the floor
const float SPAN = 1.3;   // half-width of each arch opening
const float SPRING = 2.6; // height where the columns turn into the pointed arch
const float THICK = 0.38; // stone width of columns and arch rings
const int ARCHES = 7;

// Signed distance (world units) to the edge of an equilateral pointed arch opening; >0 is stone side.
float opening(vec2 w) {
    float x = abs(w.x);
    return w.y < SPRING ? x - SPAN : length(vec2(x + SPAN, w.y - SPRING)) - 2.0 * SPAN;
}

// Laser fans in the haze from the altar at o: returns light reaching screen point p.
vec3 beams(vec2 p, vec2 o, float haze) {
    vec2 d = p - o;
    float r = length(d), ang = atan(d.y, d.x);
    float steer = 0.25 * sin(uFlow * 0.08 + uSeed * 6.28) + 0.5 * (uMid - 0.4);
    float spread = 2.1 + 0.5 * uVariant.y;
    float width = 0.005 + 0.03 * uBass, split = 0.012 + 0.03 * uHigh;
    float pulse = 1.0 + 2.5 * uBeat * exp(-pow((r - 0.08 - uBeatPhase * 1.1) * 7.0, 2.0));  // each beat runs up the beams
    vec3 col = vec3(0.0);
    for (int k = 0; k < 9; k++) {
        float fk = float(k), t = fk / 8.0 - 0.5;
        float th = 0.5 * PI + steer * (1.0 - abs(t)) + spread * t + 0.14 * sin(uFlow * (0.16 + 0.03 * fk) + fk * 1.7);
        float a = ang - th;
        float main = exp(-a * a / (width * width)) * (0.6 + 0.4 * spec(0.03 + 0.05 * abs(t)));
        float fine = exp(-pow(a - split, 2.0) / 9e-6) + exp(-pow(a + split, 2.0) / 9e-6);  // highs split each beam
        col += pal(0.62 + 0.38 * hash11(fk * 3.7 + uVariant.x * 11.0)) * (main * (0.05 / (width * 20.0 + 0.4)) * 10.0 + fine * uHigh * 0.5);
    }
    return col * pulse * haze * smoothstep(0.0, 0.06, r) / (1.0 + 2.2 * r) * step(0.0, d.y + 0.02);
}

void main() {
    vec2 uv = (gl_FragCoord.xy - 0.5 * uRes) / uRes.y;
    vec2 vp = vec2(0.04 * sin(uTime * 0.03 + uVariant.z * 6.28), 0.0);  // vanishing point
    float reveal = max(uDrop, 0.25 * uEnergySlow);                      // drops light the whole chamber
    float haze = 0.45 + 0.55 * smoothstep(-0.4, 0.6, fbm(uv * 2.5 + vec2(uFlow * 0.07, -uFlow * 0.11) + uSeed * 9.0, 3));
    vec2 altar = vp + vec2(0.0, (0.35 - CAM) / 8.0 * F);
    vec3 light = beams(uv, altar, haze);

    vec3 col = uPal[0] * (0.25 + 0.6 * reveal) + (uPal[1] * 0.1 + uPal[2] * 0.25 * reveal) * haze;
    // Rose window over the altar; each downbeat lights it.
    vec2 rw = (uv - vp - vec2(0.0, 0.1)) / 0.045;
    float rr = length(rw), spoke = abs(fract(atan(rw.y, rw.x) * 8.0 / TAU) - 0.5);
    float tracery = max(max(1.0 - smoothstep(0.04, 0.12, abs(rr - 0.95)), 1.0 - smoothstep(0.03, 0.1, abs(rr - 0.45))),
                        (1.0 - smoothstep(0.03, 0.09, spoke * rr * 2.0)) * step(0.45, rr) * step(rr, 0.95));
    float rose = (1.0 - smoothstep(0.95, 1.05, rr)) * (1.0 - 0.8 * tracery);  // glass between dark stone tracery
    col += mix(uPal[3], uPal[4], 0.3) * rose * (0.1 + 0.6 * uDownbeat + 0.5 * reveal) + uPal[3] * 0.06 * exp(-length(uv - altar) * 6.0);
    col += light;

    // Floor: polished stone that mirrors the beams faintly.
    if (uv.y < vp.y) {
        float z = F * CAM / (vp.y - uv.y);
        vec2 tile = vec2(uv.x * z / F, z + uFlow * 0.12);
        float check = mod(floor(tile.x * 1.2) + floor(tile.y * 1.2), 2.0);
        vec3 refl = beams(vec2(uv.x, 2.0 * vp.y - uv.y), altar, haze) * 0.18;
        col = mix(uPal[0], uPal[1], 0.25 + 0.2 * check) * (0.25 + 0.75 * reveal) * exp(-z * 0.08) + refl * (0.5 + 0.2 * check);
    }

    // Transverse arches from far to near, the colonnade slowly passing overhead.
    float dolly = fract(uFlow * 0.02);
    for (int k = ARCHES - 1; k >= 0; k--) {
        float z = 1.2 + (float(k) + 1.0 - dolly) * 1.1;
        vec2 w = vec2((uv.x - vp.x) * z / F, (uv.y - vp.y) * z / F + CAM);
        if (w.y < 0.0) continue;
        float d = opening(w);
        float aa = z / (F * uRes.y) * 1.5;
        float stone = smoothstep(-aa, aa, d) * (1.0 - smoothstep(THICK - aa, THICK + aa, d));
        if (stone <= 0.0) continue;
        // Masonry: voussoir joints in the arch, drums in the columns, a moulding along each edge.
        float joints = w.y < SPRING ? abs(fract(w.y * 1.6) - 0.5) : abs(fract(atan(w.y - SPRING, abs(w.x) + SPAN) * 9.0) - 0.5);
        float jointLine = smoothstep(0.42, 0.5, joints);
        float edge = exp(-d / 0.05) + 0.6 * exp(-(THICK - d) / 0.04);
        float fog = exp(-z * (0.32 - 0.2 * reveal));
        float lit = 0.12 + 2.6 * reveal;
        vec3 s = mix(uPal[1], uPal[2], 0.35 + 0.3 * hash11(float(k) + floor(uSeed * 50.0))) * lit * (1.0 - 0.35 * jointLine);
        s += light * (0.35 + 0.9 * edge);  // the beams graze the stone
        s += mix(uPal[3], uPal[4], 0.5) * edge * (0.04 + 0.3 * uBeat * exp(-float(k) * 0.4)) * fog;
        col = mix(col, s * fog + uPal[1] * 0.1 * haze * (1.0 - fog), stone);
    }
    col += light * 0.25 * haze;  // haze between us and the stone

    col *= uIntensity * 0.6 + 0.4;
    fragColor = vec4(col, 1.0);
}
