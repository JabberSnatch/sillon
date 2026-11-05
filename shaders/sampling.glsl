float uint2float(uint i) {return uintBitsToFloat(0x3F800000u | (i >> 9)) - 1.0;}
vec2 uint2float(uvec2 i) {return vec2(uint2float(i.x), uint2float(i.y));}
uint bitfieldReverse(uint x) {
    x = ((x & 0xAAAAAAAAu) >> 1) | ((x & 0x55555555u) << 1);
    x = ((x & 0xCCCCCCCCu) >> 2) | ((x & 0x33333333u) << 2);
    x = ((x & 0xF0F0F0F0u) >> 4) | ((x & 0x0F0F0F0Fu) << 4);
    x = ((x & 0xFF00FF00u) >> 8) | ((x & 0x00FF00FFu) << 8);
    return (x >> 16) | (x << 16);
}
uint interleave(uint x) {
    x &= 0x0000FFFFu;
    x = (x | (x << 8)) & 0x00FF00FFu;
    x = (x | (x << 4)) & 0x0F0F0F0Fu;
    x = (x | (x << 2)) & 0x33333333u;
    x = (x | (x << 1)) & 0x55555555u;
    return x;
}
uint morton2D(uvec2 p) {return interleave(p.x) | (interleave(p.y) << 1);}
uint OwenHash4(uint x, uint seed) {x = bitfieldReverse(x); x ^= x * 0x3D20ADEAu; x ^= (x >> 1) & (x << 1) & 0x55555555u; x += seed; x *= (seed >> 16) | 1u; x ^= (x >> 1) & (x << 1) & 0x55555555u; x ^= x * 0x05526c56u; x ^= x * 0x53a22864u; return 0xFFFF0000u & x;}
uvec2 plasticSequence(uint i) {return i * uvec2(3242174889u, 2447445414u);}
vec2 pixelPlasticSequence(uint i) { return 1.05 * fract(uint2float(plasticSequence(i)) + vec2(0.5)) -  vec2(0.5); }
vec2 OwenPlastic(uint i, uint seed) {return uint2float(plasticSequence(bitfieldReverse(OwenHash4(i, seed ^ 0xF8ADE99Au))));}
mat3 genTB(vec3 N) {
     float s = N.z < 0.0 ? -1.0 : 1.0;
     float a = -1.0 / (s + N.z);
     float b = N.x * N.y * a;
     return mat3(
         vec3(1.0 + s * N.x * N.x * a, s * b, -s * N.x),
         vec3(b, s + N.y * N.y * a, -N.y),
         N);
 }
