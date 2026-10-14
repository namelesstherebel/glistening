// Separable gaussian, 9 taps folded into 5 bilinear fetches.
uniform sampler2D uSrc;
uniform vec2 uDir;
out vec4 o;
void main() {
    vec3 c = texture(uSrc, vUv).rgb * 0.2270270;
    c += (texture(uSrc, vUv + uDir * 1.3846).rgb + texture(uSrc, vUv - uDir * 1.3846).rgb) * 0.3162162;
    c += (texture(uSrc, vUv + uDir * 3.2308).rgb + texture(uSrc, vUv - uDir * 3.2308).rgb) * 0.0702703;
    o = vec4(c, 1.0);
}
