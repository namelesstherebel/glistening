// @name Magnetic Field
// @desc glowing filings tracing three invisible magnets, one per band; kicks snap the filings into line
// @busy 0.6
// @bloom 0.8
// @palettes glacier,silver,ember,abyss
out vec4 fragColor;

const float LINES = 14.0;  // field lines leaving each pole; an integer keeps the stream function seamless
const float CELL = 0.014;  // filing grid, screen units

vec2 poles[6];
float sgn[6];

// Three dipoles: bass sets the spread of the first, mids turn the second, highs shake the third.
void magnets() {
    vec2 c[3] = vec2[](vec2(-0.42, 0.04), vec2(0.4, 0.08), vec2(0.0, -0.2));
    float band[3] = float[](uBass, uMid, uHigh);
    for (int i = 0; i < 3; i++) {
        float fi = float(i);
        vec2 o = c[i] + 0.1 * vec2(sin(uFlow * 0.03 + fi * 2.1 + uSeed * 6.28), cos(uFlow * 0.025 + fi * 1.3 + uVariant.x * 6.28));
        float ang = uVariant.y * 6.28 + fi * 1.9 + uFlow * (0.01 + 0.01 * fi);
        float spread = 0.1;
        if (i == 0) spread = 0.1 + 0.2 * band[0];
        if (i == 1) ang += 2.2 * band[1];
        if (i == 2) o += 0.05 * band[2] * vec2(sin(uTime * 2.3), cos(uTime * 1.9)), spread = 0.06 + 0.08 * band[2];
        vec2 d = spread * vec2(cos(ang), sin(ang));
        poles[2 * i] = o + d, poles[2 * i + 1] = o - d;
        sgn[2 * i] = 1.0, sgn[2 * i + 1] = -1.0;
    }
}

// Stream function (field lines are its contours), its gradient, and each dipole's field strength.
float stream(vec2 p, out vec2 grad, out vec3 strength) {
    float psi = 0.0;
    grad = vec2(0.0), strength = vec3(0.0);
    for (int i = 0; i < 6; i++) {
        vec2 d = p - poles[i];
        float r2 = max(dot(d, d), 2e-4);
        psi += sgn[i] * atan(d.y, d.x);
        vec2 g = sgn[i] * vec2(-d.y, d.x) / r2;
        grad += g;
        strength[i / 2] += 1.0 / r2;
    }
    return psi;
}

void main() {
    vec2 uv = (gl_FragCoord.xy - 0.5 * uRes) / uRes.y;
    magnets();
    vec3 bandCol[3] = vec3[](pal(0.55), pal(0.78), mix(pal(1.0), vec3(1.0), 0.2));

    // Faint continuous field lines.
    vec2 g;
    vec3 st;
    float psi = stream(uv, g, st);
    float u = psi * LINES / TAU;
    float px = length(g) * LINES / TAU / uRes.y + 1e-5;  // contour spacing per pixel
    float e = abs(fract(u + 0.5) - 0.5);
    float line = (1.0 - smoothstep(0.0, 1.5 * px, e)) * (1.0 - smoothstep(0.15, 0.45, px));
    vec3 mixCol = (bandCol[0] * st.x + bandCol[1] * st.y + bandCol[2] * st.z) / (st.x + st.y + st.z);
    vec3 col = uPal[0] * 0.6 + mixCol * line * (0.08 + 0.12 * uEnergySlow);

    // Filings: one grain per cell, turned along the field at its centre; kicks pull them into line.
    vec2 cg = uv / CELL, id = floor(cg);
    vec3 h = hash33(vec3(id, floor(uSeed * 100.0)));
    vec2 c = (id + 0.5 + (h.xy - 0.5) * 0.3) * CELL;
    vec2 gc;
    vec3 sc;
    float pc = stream(c, gc, sc);
    float strong = smoothstep(4.0, 60.0, length(gc));  // filings gather where the field is strong
    float near = abs(fract(pc * LINES / TAU + 0.5) - 0.5);
    float onLine = 1.0 - smoothstep(0.08, 0.3, near);
    float align = clamp(0.25 + 0.9 * uBeat + 0.35 * uBass, 0.0, 1.0);
    float ang = atan(gc.x, -gc.y) + (h.z - 0.5) * 2.4 * (1.0 - align);  // the field direction is grad rotated back
    vec2 dir = vec2(cos(ang), sin(ang));
    vec2 q = (uv - c) / CELL;
    float along = dot(q, dir), across = dot(q, vec2(-dir.y, dir.x));
    float grain = (1.0 - smoothstep(0.3, 0.44, abs(along))) * (1.0 - smoothstep(0.06, 0.14, abs(across)));
    vec3 gcol = (bandCol[0] * sc.x + bandCol[1] * sc.y + bandCol[2] * sc.z) / (sc.x + sc.y + sc.z);
    float glint = step(1.0 - 0.04 * uHigh, hash12(id + floor(uTime * 8.0)));  // highs catch single grains
    float lum = (0.2 + 0.8 * strong) * (0.12 + 0.88 * onLine) * (0.9 + 0.8 * uBeat + 0.5 * uEnergySlow) * 1.6 + 2.0 * glint;
    col += gcol * grain * lum * (0.35 + 0.3 * h.x);

    col *= 1.0 - 0.3 * smoothstep(0.4, 1.0, length(uv));
    col *= uIntensity * 0.6 + 0.4;
    fragColor = vec4(col, 1.0);
}
