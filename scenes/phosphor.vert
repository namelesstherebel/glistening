// @particles 1023
// The waveform bent into a ring and mirrored about one axis, drawn as a CRT beam:
// one capsule per sample step. The fragment pass pushes every trace outward as it
// cools, so the last second of sound stands around the beam as fading rings.

float body(float x) {  // low-passed sample: the beam traces the body of the sound, not the hiss
    const float e = 5.0 / 1024.0;
    return (wave(x - 2.0 * e) + 2.0 * wave(x - e) + 3.0 * wave(x) + 2.0 * wave(x + e) + wave(x + 2.0 * e)) / 9.0;
}

float trigger() {  // first rising zero crossing: every frame starts on the same phase, so the rings nest
    float prev = body(0.03);
    for (int k = 1; k <= 48; k++) {
        float x = 0.03 + float(k) * 0.009, s = body(x);
        if (prev < 0.0 && s >= 0.0) return x;
        prev = s;
    }
    return 0.03;
}

vec2 point(float i, float amp, float from) {
    float u = i / (uCount - 1.0), x = 1.0 - abs(2.0 * u - 1.0);  // 0..1..0: both halves read the same samples, so the ring closes
    float t = uTime * 0.5;
    float idle = 0.03 * cos(3.0 * PI * x) * sin(t * 1.3) + 0.015 * cos(5.0 * PI * x) * sin(t * 0.9 + uSeed * TAU);  // slow standing wave in silence
    float taper = smoothstep(0.0, 0.07, x) * smoothstep(0.0, 0.07, 1.0 - x);  // no cusp on the mirror axis
    float r = 0.40 + 0.04 * uBass + 0.06 * uBeat + 0.04 * uDrop + mix(idle, 0.2 * body(from + 0.5 * x) * taper, amp);
    float th = u * TAU + uVariant.x * TAU + uFlow * 0.03 * (uVariant.y > 0.5 ? 1.0 : -1.0);
    vec2 p = r * vec2(cos(th), sin(th));
    p.x *= uRes.y / uRes.x;
    return p;
}

void main() {
    float i = float(gl_InstanceID);
    if (i > uCount - 2.0) { hide(); return; }
    float amp = smoothstep(0.02, 0.25, uEnergy + uBass);
    float from = trigger();
    vec2 a = point(i, amp, from), b = point(i + 1.0, amp, from);
    float lenPx = length((b - a) * uRes * 0.5);
    float bright = 3.0 / (lenPx + 2.5) * (0.6 + 0.4 * amp) * (1.0 + 1.4 * uBeat * uBeat + 0.8 * uDrop + 0.3 * uHigh);  // slow beam = bright
    sprite(a, b, (0.9 + 0.3 * uBass + 0.5 * uBeat) * (uRes.y / 540.0) * 1.3, mix(uPal[4], vec3(1.0), 0.25), bright * uIntensity);
}
