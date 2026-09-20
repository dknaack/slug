#version 300 es

// WebGL fragment shaders require explicit precision declarations
precision highp float;
precision highp int;
precision highp isampler2D;

in vec2 frag_texcoord;
flat in int frag_contour_count;
flat in int frag_contour_offset;
flat in int frag_point_offset;

out vec4 color;

uniform sampler2D point_data;
uniform isampler2D contour_data;

vec2 get_point(int i)
{
    int offset = frag_point_offset + i;
    vec2 p = texelFetch(point_data, ivec2(offset % 256, offset / 256), 0).rg;
    vec2 result = p - frag_texcoord;
    return result;
}

int get_contour(int i)
{
    int offset = frag_contour_offset + i;
    int result = texelFetch(contour_data, ivec2(offset % 64, offset / 64), 0).r;
    return result;
}

void main()
{
    const float epsilon = 0.0001;

    float coverage = 0.0;
    int contour_start = 0;
    vec2 pixels_per_em = vec2(1.0 / fwidth(frag_texcoord.x), 1.0 / fwidth(frag_texcoord.y));

    for (int i = 0; i < frag_contour_count; i++) {
        int contour_end = get_contour(i);
        int point_count = contour_end - contour_start;
        for (int j = 0; j < point_count; j += 2) {
            vec2 p0 = get_point(contour_start + (j + 0));
            vec2 p1 = get_point(contour_start + (j + 1) % point_count);
            vec2 p2 = get_point(contour_start + (j + 2) % point_count);

            uint shift = ((p0.y > 0.0) ? 2U : 0U) + ((p1.y > 0.0) ? 4U : 0U) + ((p2.y > 0.0) ? 8U : 0U);
            uint code = (0x2E74U >> shift) & 3U;
            if (code != 0U) {
                float ay = p0.y - p1.y * 2.0 + p2.y;
                float by = p0.y - p1.y;
                float cy = p0.y;

                float t1, t2;
                if (abs(ay) < epsilon) {
                    t1 = t2 = p0.y / (2.0 * by); // Fixed: 2 -> 2.0 (strict type matching)
                } else {
                    float d = sqrt(max(by * by - ay * cy, 0.0));
                    t1 = (by - d) / ay;
                    t2 = (by + d) / ay;
                }

                float ax = p0.x - p1.x * 2.0 + p2.x;
                float bx = p0.x - p1.x;

                if ((code & 1U) != 0U) {
                    float x1 = (ax * t1 - bx * 2.0) * t1 + p0.x;
                    coverage += clamp(x1 * pixels_per_em.x + 0.5, 0.0, 1.0);
                }

                if ((code & 2U) != 0U) { // Fixed: 0 -> 0U (uint comparison)
                    float x2 = (ax * t2 - bx * 2.0) * t2 + p0.x;
                    coverage -= clamp(x2 * pixels_per_em.x + 0.5, 0.0, 1.0);
                }
            }
        }

        contour_start = contour_end;
    }

    color = vec4(1.0);
    float alpha = coverage * color.w;
    color = vec4(color.xyz * alpha, alpha);
}
