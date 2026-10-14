// @name Phosphor
// @desc the waveform as a ring drawn by a CRT beam; each trace drifts outward as it cools
// @busy 0.7
// @bloom 1.1
// @palettes moss,ember,glacier,abyss
out vec4 fragColor;

void main() {
    vec2 c = gl_FragCoord.xy - 0.5 * uRes, p = c / uRes.y;
    // Glass: deep backdrop with a faint curved sheen.
    vec3 bg = uPal[0] * 0.25 + uPal[1] * 0.03 * (1.0 - smoothstep(0.0, 1.0, length(p - vec2(-0.15, 0.2))));
    // Afterglow: the previous frame, pushed outward (harder on beats) and turned a little.
    float push = 0.1 + 0.2 * uEnergySlow + 3.0 * uBeat * uBeat + 0.8 * uDrop;  // rings drift between beats and jump on them
    vec2 src = rot(0.06 * uDt * (uVariant.y > 0.5 ? 1.0 : -1.0)) * c / (1.0 + push * uDt);
    vec3 prev = max(texture(uPrev, (src + 0.5 * uRes) / uRes).rgb - bg, 0.0);
    // It decays and cools from the beam colour toward the phosphor's tail colour.
    float l = dot(prev, vec3(0.3, 0.5, 0.2));
    vec3 tail = mix(prev, l * mix(uPal[3], uPal[2], 0.5) * 1.6, 1.0 - exp(-uDt * 4.0));
    fragColor = vec4(bg + tail * exp(-uDt * (4.2 - 1.4 * uEnergySlow)), 1.0);  // longer trails when the music is full
}
