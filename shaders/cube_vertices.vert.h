R"_(

const uint kTriIndices[] = uint[36](
#ifndef CW_CUBE
    // COUNTERCLOCKWISE FACES
    0, 1, 2,  2, 1, 3,
    0, 4, 1,  1, 4, 5,
    3, 1, 7,  7, 1, 5,
    7, 5, 6,  6, 5, 4,
    2, 6, 0,  0, 6, 4,
    2, 3, 6,  6, 3, 7
#else
    // CLOCKWISE FACES
    0, 2, 1,  1, 2, 3,
    0, 1, 4,  4, 1, 5,
    3, 7, 1,  1, 7, 5,
    7, 6, 5,  5, 6, 4,
    2, 0, 6,  6, 0, 4,
    2, 6, 3,  3, 6, 7
#endif
    );

const vec3 kTriVertices[] = vec3[8](
    vec3(-1.0, -1.0, -1.0),
    vec3(-1.0, -1.0, 1.0),
    vec3(-1.0, 1.0, -1.0),
    vec3(-1.0, 1.0, 1.0),
    vec3(1.0, -1.0, -1.0),
    vec3(1.0, -1.0, 1.0),
    vec3(1.0, 1.0, -1.0),
    vec3(1.0, 1.0, 1.0)
    );

const vec2 kVertexUV[] = vec2[4](
    vec2(0.0, 0.0),
    vec2(0.0, 1.0),
    vec2(1.0, 0.0),
    vec2(1.0, 1.0)
    );

const uint kUVIndices[] = uint[36](
    0, 1, 2,  2, 1, 3,
    0, 1, 2,  2, 1, 3,
    0, 1, 2,  2, 1, 3,
    0, 1, 2,  2, 1, 3,
    0, 1, 2,  2, 1, 3,
    0, 1, 2,  2, 1, 3
    );

vec3 CurrentVertex()
{
    return kTriVertices[kTriIndices[gl_VertexIndex]];
}

vec2 CurrentUV()
{
    return kVertexUV[kUVIndices[gl_VertexIndex]];
}

)_"
