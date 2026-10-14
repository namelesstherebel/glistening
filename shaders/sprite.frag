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
    float along = vHalf < 0.05 ? exp(-2.0 * vLocal.x * vLocal.x)
                               : 0.5 * (erf1((vLocal.x + vHalf) * 1.4142) - erf1((vLocal.x - vHalf) * 1.4142)) * min(1.0, vHalf * 2.0);
    o = vec4(vColor.rgb * vColor.a * across * along, 0.0);
}
