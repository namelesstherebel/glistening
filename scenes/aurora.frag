// @name Aurora
// @desc curtains of light folding over a mountain lake
// @busy 0.15
// @bloom 0.9
// @palettes aurora,glacier,orchid,moss
out vec4 fragColor;

// Each curtain is a vertical sheet hanging above a snaking line z = curve(x).
// A ray finds where its footprint crosses that line; the sheet glows from a
// crisp lower hem upward, and folds seen edge-on burn brightest.

const float HORIZON = -0.24;  // screen y of the horizon, -0.5..0.5
const float BASE = 1.9;       // altitude of the hem

float n1(float x) {  // smooth 1D noise
    float i = floor(x), f = fract(x);
    return mix(hash11(i), hash11(i + 1.0), f * f * (3.0 - 2.0 * f)) * 2.0 - 1.0;
}
float g1(float x) {  // 1D gradient noise, quintic: smooth curves, cheap
    float i = floor(x), f = fract(x), u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    return 2.0 * mix((hash11(i) * 2.0 - 1.0) * f, (hash11(i + 1.0) * 2.0 - 1.0) * (f - 1.0), u);
}
float curve(float x, float k) {
    float f = uFlow * 0.05 * (1.0 - 1.6 * k);
    float z = 5.0 + 3.5 * k;
    z += 3.0 * g1(x * 0.13 + f + k * 13.0 + uSeed * 20.0) + 1.6 * g1(x * 0.5 - f * 1.4 + k * 7.0 + 3.0)
       + (0.25 + 0.35 * uMid) * g1(x * 1.4 + f * 2.0 + k + 9.0);
    z += 0.6 * uDownbeat * sin(x * 0.7 - uTime * 2.5 + k * 2.0);  // a fold runs along the curtain each bar
    return 2.2 + log(1.0 + exp(z - 2.2));  // never closer than ~2.2
}

float erf1(float x) {  // erf, max error ~1e-4
    float x2 = x * x, a = 0.147;
    return sign(x) * sqrt(1.0 - exp(-x2 * (1.2732 + a * x2) / (1.0 + a * x2)));
}

// Each sheet has a thickness; along the ray's footprint the signed distance to
// the sheet is taken as linear per step, so the path length through the sheet
// integrates exactly with erf: crossings, grazing folds (seen edge-on, long
// paths, brightest) and near misses all come out smooth.
vec3 curtains(vec3 rd, bool mirror) {
    vec3 acc = vec3(0.0);
    vec2 d = normalize(rd.xz);
    float len = length(rd.xz);
    const float W = 0.22;  // sheet half-thickness
    float top = 0.55 + 0.45 * uBass + 0.35 * uDrop;
    int steps = mirror ? 28 : int(30.0 + 20.0 * uQuality);
    for (int k = 0; k < 2; k++) {
        float fk = float(k);
        float s0 = 1.5, f0 = d.y * s0 - curve(d.x * s0, fk), ds = 0.35;
        for (int i = 0; i < 60; i++) {
            if (i >= steps) break;
            float s1 = s0 + ds;
            vec2 q1 = d * s1;
            float f1 = q1.y - curve(q1.x, fk);
            if (min(abs(f0), abs(f1)) < 3.0 * W || sign(f0) != sign(f1)) {
                float df = f1 - f0;
                float path = abs(df) > 1e-4 ? ds * W * 0.8862 * (erf1(f1 / W) - erf1(f0 / W)) / df : ds * exp(-f0 * f0 / (W * W));
                float sc = mix(s0, s1, clamp(f0 / (f0 - f1 + 1e-6), 0.0, 1.0));
                vec2 q = d * sc;
                float h = sc / len * rd.y - BASE;  // altitude above the hem where the ray meets the sheet
                float hem = smoothstep(-0.03, 0.0, h) * (exp(-max(h, 0.0) / 0.035) * 1.6 + exp(-max(h, 0.0) / (0.12 + 0.1 * uMid)));
                float veil = smoothstep(-0.02, 0.15, h) * exp(-max(h, 0.0) / top) * 0.14;
                float ray = n1(q.x * 7.0 + fk * 31.0 + uFlow * 0.15) * 0.5 + 0.5;
                ray = mix(1.0, ray * ray * 1.8, 0.7) * (0.85 + (0.2 + 0.5 * uHigh) * n1(q.x * 23.0 - uTime * 1.5));
                float band = 0.6 + 0.8 * spec(fract(q.x * 0.03 + 0.5 + fk * 0.37));
                vec3 low = mix(uPal[3], uPal[4], 0.15 + 0.35 * uBeat + 0.3 * uDrop);
                vec3 high = uPal[2] * 1.4 + uPal[4] * 0.15;
                vec3 c = mix(mix(mix(uPal[3], vec3(1.0), 0.55), low, smoothstep(0.0, 0.05, h)), high, smoothstep(0.05, top, h));  // white-hot hem, green body, violet crown
                float lum = (hem * (1.0 + 0.7 * uBeat) + veil) * ray * band * min(path / W, 7.0) * 0.3;
                acc += c * lum * exp(-sc * 0.035) * (1.0 - 0.35 * fk);
                acc += uPal[3] * 0.02 * exp(min(h, 0.0) * 25.0) * step(h, 0.0) * min(path / W, 4.0);  // airglow under the hem
            }
            s0 = s1, f0 = f1;
            ds *= 1.07;
        }
    }
    float k = 0.55 + 0.6 * uEnergySlow + 0.45 * uBass + 0.4 * uDrop;
    return acc * k * uIntensity;
}

float ridge(float x, float far) {  // mountain silhouettes, two ranges
    float s = uSeed * 37.0 + far * 11.0;
    float r = 1.0 - abs(gnoise(vec2(x * (1.3 + far) + s, s)));
    float m = 0.01 + (0.11 - 0.07 * far) * r * r * (0.45 + 0.55 * smoothstep(-0.3, 0.5, gnoise(vec2(x * 0.4 - s, 2.0))));
    return m + 0.008 * gnoise(vec2(x * 13.0, s)) + 0.003 * gnoise(vec2(x * 41.0, s));
}

vec3 sky(vec3 rd, vec2 sp, bool mirror) {
    float h = rd.y;
    vec3 c = mix(uPal[1] * 0.1 + uPal[3] * 0.008, uPal[0] * 0.5, smoothstep(0.0, 0.45, h));
    vec2 g = sp * 90.0;  // stars: sparse, a few bright ones
    vec2 id = floor(g);
    float r = hash12(id + uSeed * 100.0);
    if (r > 0.955) {
        vec2 o = hash22(id) - 0.5;
        float d = length(fract(g) - 0.5 - o * 0.6);
        float b = pow((r - 0.955) / 0.045, 3.0);
        float tw = 0.75 + 0.25 * sin(uTime * (2.0 + 4.0 * r) + r * 40.0) * (0.3 + uHigh);
        c += mix(uPal[4], vec3(1.0), 0.5) * exp(-d * d * 60.0) * (0.04 + 1.1 * b) * tw * smoothstep(0.0, 0.06, h);
    }
    if (h > 0.0) c += curtains(rd, mirror);
    return c;
}

vec3 scene(vec2 uv, bool mirror) {
    vec3 rd = normalize(vec3(uv.x, uv.y - HORIZON, 1.3));
    float y = uv.y - HORIZON;
    float near = ridge(uv.x, 0.0), far = ridge(uv.x + 3.7, 1.0);
    if (y > max(near, far)) return sky(rd, uv, mirror);
    if (y > near) return mix(uPal[0] * 0.3, uPal[1] * 0.16, y / far);  // far range: hazy, lit from above
    return uPal[0] * 0.2;                                                  // near range: black
}

void main() {
    vec2 uv = (gl_FragCoord.xy - 0.5 * uRes) / uRes.y;
    uv.x += (uVariant.x - 0.5) * 0.6;
    float y = uv.y - HORIZON;
    vec3 col;
    if (y >= 0.0) col = scene(uv, false);
    else {
        // Lake: a darker mirror; ripples stir with the bass.
        float dist = 0.35 / max(-y, 0.004);
        vec2 wp = vec2(uv.x * dist, dist);
        float rip = (gnoise(wp * vec2(0.8, 2.5) + vec2(0.0, uFlow * 0.25)) + 0.4 * gnoise(wp * vec2(2.0, 6.0) - uTime * 0.2))
                  * (0.003 + 0.012 * uBass) * (1.0 - smoothstep(1.0, 40.0, dist));
        float fres = 0.35 + 0.5 * pow(1.0 - min(1.0, -y * 3.5), 3.0);
        col = scene(vec2(uv.x + rip * 0.3, HORIZON - y * 1.8 + rip), true) * fres + uPal[1] * 0.015;  // stretched: the lake holds the ribbons
    }
    vec3 prev = texture(uPrev, gl_FragCoord.xy / uRes).rgb;
    fragColor = vec4(mix(col, prev, pow(0.25, uDt * 30.0)), 1.0);  // light persistence smooths the rays (0.25 per 1/30 s)
}
