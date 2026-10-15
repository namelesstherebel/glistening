// Particles, additive. A dot is a gaussian; a segment is that gaussian swept
// along its length (erf), so chained segments add up to an even line.
in vec2 vLocal;
in float vHalf;
in vec4 vColor;
out vec4 o;
float erf1(float x) {
    float x2 = x * x;
    return sign(x) * sqrt(1.0 - exp(-x2 * (1.2732 + 0.147 * x2) / (1.0 + 0.147 * x2)));
}
void main() {
    float across = exp(-2.0 * vLocal.y * vLocal.y);
    float along = exp(-2.0 * vLocal.x * vLocal.x);
    if (vHalf > 0.01) {
        // Normalize shrinking segments to a dot's energy. Blend the dot limit
        // before erf subtraction loses precision for very short segments.
        float segment = 0.5 * (erf1((vLocal.x + vHalf) * 1.4142) - erf1((vLocal.x - vHalf) * 1.4142)) * max(1.0, 0.6266571 / vHalf);
        along = mix(along, segment, smoothstep(0.01, 0.05, vHalf));
    }
    o = vec4(vColor.rgb * vColor.a * across * along, 0.0);
}
