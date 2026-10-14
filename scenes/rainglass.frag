// @name Rain Glass
// @desc rain on a window over a blurred neon street; bass ripples the puddles, mids drag drops into trails, highs tap the glass
// @busy 0.1
// @bloom 0.9
// @palettes neon,glacier,dusk,abyss
out vec4 fragColor;

const float HORIZON = -0.17;  // kerb line of the street behind the glass

float boxDist(vec2 p, vec2 b) {
    vec2 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
}

// The street above the kerb: dark facades, neon signs and bokeh lights.
// sharp 0 = seen through fogged glass, 1 = seen through a drop or a cleared trail.
vec3 street(vec2 p, float sharp) {
    vec3 col = mix(uPal[1] * 0.5, uPal[0] * 0.4, smoothstep(HORIZON, 0.55, p.y));
    float w = mix(0.035, 0.012, sharp);  // glow width: blur of the fog
    // Signs: glowing tube outlines; each follows its own band, so bass, mids and highs light different signs.
    for (int i = 0; i < 4; i++) {
        float fi = float(i);
        vec2 c = vec2((hash11(fi * 7.13 + uSeed * 13.0) - 0.5) * 1.5, HORIZON + 0.1 + 0.28 * hash11(fi * 3.31 + uSeed * 5.0));
        vec2 b = hash11(fi * 4.3 + uSeed * 2.0) < 0.5 ? vec2(0.05 + 0.12 * hash11(fi + 9.7 + uSeed), 0.02 + 0.04 * hash11(fi * 1.7 + 2.0 + uSeed))
                                                      : vec2(0.018 + 0.015 * hash11(fi + 9.7 + uSeed), 0.08 + 0.1 * hash11(fi * 1.7 + 2.0 + uSeed));  // banner or upright
        float d = abs(boxDist(p - c, b)) - 0.003;
        float band = 0.06 + 0.3 * fi;
        float lit = 0.35 + 1.3 * spec(band) + 0.25 * uBeat * step(fi, 1.5);
        col += pal(0.6 + 0.4 * hash11(fi * 5.9 + uVariant.x * 9.0)) * lit * (0.006 / (d * d + w * w)) * w;
    }
    // Bokeh: window and street lights as soft discs, two depths.
    for (int l = 0; l < 2; l++) {
        float s = l == 0 ? 3.5 : 7.0;
        vec2 g = p * s + vec2(uVariant.y * 31.0 + float(l) * 7.0, 0.0);
        vec2 id = floor(g), f = fract(g) - 0.5;
        vec2 h = hash22(id + uSeed * 17.0);
        float r = 0.14 + 0.2 * h.y, edge = mix(0.3, 0.05, sharp);
        float disc = 1.0 - smoothstep(r - edge, r, length(f - (h - 0.5) * 0.35));
        float on = step(0.4, h.x) * smoothstep(HORIZON - 0.02, HORIZON + 0.06, p.y) * (1.0 - smoothstep(0.25, 0.55, p.y));
        col += pal(0.35 + 0.65 * h.y) * disc * on * (0.1 + 0.3 * spec(h.x)) * (l == 0 ? 0.6 : 1.0);
    }
    return col;
}

// Wet street below the kerb: the facades reflected, ringed by bass on every beat and by the rain.
vec3 puddle(vec2 p, float sharp) {
    vec2 q = vec2(p.x, 2.0 * HORIZON - p.y);
    float crest = 0.0;
    for (int k = 0; k < 3; k++) {  // one ring per recent beat, the newest strongest
        float b = uBeats - float(k), age = float(k) + uBeatPhase;
        vec2 c = vec2(hash11(b * 1.37 + uSeed) * 1.6 - 0.8, HORIZON - 0.06 - 0.25 * hash11(b * 2.11 + 4.0));
        float r = length((p - c) * vec2(1.0, 3.2)), R = 0.05 + age * 0.16;
        float ring = sin((r - R) * 70.0) * exp(-pow((r - R) * 12.0, 2.0)) * exp(-age * 0.8) * (0.25 + 1.2 * uBass);
        q.y += ring * 0.02, crest += max(ring, 0.0);
    }
    vec2 g = p * vec2(14.0, 40.0), id = floor(g);  // light rain rings, squashed by perspective
    float a = fract(uTime * 0.7 + hash12(id)), rr = length((fract(g) - 0.5) * vec2(1.0, 0.4));
    crest += 0.3 * (1.0 - a) * exp(-pow((rr - a * 0.45) * 18.0, 2.0)) * step(0.6, hash12(id + 3.0));
    float depth = clamp((HORIZON - p.y) / 0.35, 0.0, 1.0);
    vec3 col = street(q, sharp) * (0.55 - 0.3 * depth) + uPal[0] * 0.15;
    return col + mix(uPal[3], uPal[4], 0.5) * crest * (0.05 + 0.12 * uBeat) * (1.0 - 0.6 * depth);
}

vec3 view(vec2 p, float sharp) { return p.y > HORIZON ? street(p, sharp) : puddle(p, sharp); }

// A lens drop of radius r at offset d: .xy refraction vector, .z coverage.
vec3 lens(vec2 d, float r) {
    if (r <= 0.0) return vec3(0.0);  // absent drop: no division, no degenerate smoothstep
    float m = 1.0 - smoothstep(0.75 * r, r, length(d));
    return vec3(d / r, m);
}
vec3 keep(vec3 a, vec3 b) { return b.z > a.z ? b : a; }

void main() {
    vec2 uv = (gl_FragCoord.xy - 0.5 * uRes) / uRes.y;
    vec3 drop = vec3(0.0);
    float clear = 0.0;  // fog wiped away by trails

    // Sliding drops: one per tall cell; mids lengthen the trail of beads and speed the slide.
    vec2 cell = vec2(1.0 / 7.0, 0.5);
    vec2 g = uv / cell;
    float colShift = hash11(floor(g.x) + uSeed * 9.0);
    g.y += colShift * 3.0;
    vec2 id = floor(g), f = fract(g);
    vec3 h = hash33(vec3(id, uSeed * 5.0));
    float tt = uFlow * (0.025 + 0.02 * h.z) + h.y;
    float y = 0.9 - 0.8 * fract(tt);
    y += 0.05 * sin(fract(tt) * TAU * 3.0);  // stick and slip
    float x = 0.5 + (h.x - 0.5) * 0.5 + 0.12 * sin(uv.y * 22.0 + h.z * 6.0) * (h.z - 0.5);
    vec2 dd = (f - vec2(x, y)) * cell;
    if (h.z > 0.25) {
        drop = keep(drop, lens(dd * vec2(1.0, 0.8), 0.028 + 0.01 * h.x));
        float trail = smoothstep(0.0, 0.02, f.y - y) * (1.0 - smoothstep(0.0, 0.15 + 0.7 * uMid, f.y - y));
        clear = max(clear, trail * (1.0 - smoothstep(0.008, 0.022, abs(dd.x))));
        float bead = fract(uv.y * 32.0 + h.x) - 0.5;
        drop = keep(drop, lens(vec2(dd.x, bead / 32.0), 0.008 * trail) * vec3(1.0, 1.0, trail * (0.35 + 0.65 * uMid)));
    }
    // Static drops condense and evaporate.
    for (int l = 0; l < 2; l++) {
        float s = l == 0 ? 22.0 : 60.0;
        vec2 sg = uv * s + float(l) * 3.7, sid = floor(sg);
        vec3 sh = hash33(vec3(sid, float(l) + uSeed * 3.0));
        float life = sin(fract(uTime * 0.04 + sh.z) * PI);
        vec2 sd = (fract(sg) - 0.5 - (sh.xy - 0.5) * 0.5) / s;
        drop = keep(drop, lens(sd, (0.25 + 0.2 * sh.x) / s * life * step(l == 0 ? 0.72 : 0.5, sh.y)));
    }

    vec3 back = view(uv, clear);
    vec3 through = view(uv - drop.xy * 0.12, 1.0);  // a drop is a lens: a small, flipped, sharp view
    vec3 col = back * (0.75 + 0.25 * clear) + uPal[2] * 0.025 * (1.0 - clear);  // fog scatters a little light
    vec3 dropCol = through * 1.3 + mix(uPal[4], vec3(1.0), 0.5) * pow(max(dot(drop.xy, vec2(-0.6, 0.6)), 0.0), 6.0) * 0.6;
    dropCol *= 1.0 - 0.55 * smoothstep(0.6, 0.98, length(drop.xy));  // dark rim
    col = mix(col, dropCol, drop.z);

    // Highs: tiny taps on the glass, each a bright bead and a quick ring.
    vec2 ig = uv * 18.0, iid = floor(ig);
    float slot = uTime * 2.5 + hash12(iid);
    float ih = hash12(iid + floor(slot) * 1.31), age = fract(slot);
    vec2 io = fract(ig) - 0.5 - (hash22(iid + floor(slot)) - 0.5) * 0.6;
    float ir = length(io) / 18.0, on = step(ih, 0.005 + 0.08 * uHigh);
    float tap = (exp(-ir * ir * 8e5) + 0.6 * exp(-pow((ir - 0.002 - age * 0.012) * 900.0, 2.0))) * (1.0 - age) * (1.0 - age);
    col += mix(uPal[4], vec3(1.0), 0.3) * tap * on * (0.3 + 0.9 * uHigh);

    col *= uIntensity * 0.6 + 0.4;
    fragColor = vec4(col, 1.0);
}
