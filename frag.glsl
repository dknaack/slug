#version 410 core

in vec2 pos;

uniform sampler1D point_data;
uniform isampler1D contour_data;

out vec4 frag_color;

void main()
{
	const float epsilon = 0.0001;

	float coverage = 0.0;
	frag_color = vec4(1.0);

	vec2 pixels_per_em = vec2(1.0 / fwidth(pos.x), 1.0 / fwidth(pos.y));

	int num_crossings = 0;
	int contour_count = 2;
	int contour_start = 0;
	for (int i = 0; i < contour_count; i++) {
		int contour_end = texelFetch(contour_data, i, 0).r;
		int point_count = contour_end - contour_start;
		for (int j = 0; j < point_count; j += 2) {
			vec2 p0 = texelFetch(point_data, contour_start + (j + 0),               0).rg;
			vec2 p1 = texelFetch(point_data, contour_start + (j + 1) % point_count, 0).rg;
			vec2 p2 = texelFetch(point_data, contour_start + (j + 2) % point_count, 0).rg;

			p0 -= pos;
			p1 -= pos;
			p2 -= pos;

			uint shift = ((p0.y > 0.0) ? 2U : 0U) + ((p1.y > 0.0) ? 4U : 0U) + ((p2.y > 0.0) ? 8U : 0U);
			uint code = (0x2E74U >> shift) & 3U;
			if (code != 0U) {
				float ay = p0.y - p1.y * 2.0 + p2.y;
				float by = p0.y - p1.y;
				float cy = p0.y;

				float t1, t2;
				if (abs(ay) < epsilon) {
					t1 = t2 = p0.y / (2 * by);
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

				if ((code & 2U) != 0) {
					float x2 = (ax * t2 - bx * 2.0) * t2 + p0.x;
					coverage -= clamp(x2 * pixels_per_em.x + 0.5, 0.0, 1.0);
				}
			}
		}

		contour_start = contour_end;
	}

	vec4 vcolor = vec4(0.0, 1.0, 1.0, 1.0);
	coverage = sqrt(clamp(abs(coverage) * 0.5, 0.0, 1.0));
	float alpha = coverage * vcolor.w;
	frag_color = vec4(vcolor.xyz * alpha, alpha);
}

