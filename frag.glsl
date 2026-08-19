#version 410 core

in vec2 pos;

uniform sampler1D point_data;
uniform isampler1D contour_data;

out vec4 frag_color;

int intersects(vec2 p, vec2 p0, vec2 p1, vec2 p2)
{
    float a = p0.y - 2.0*p1.y + p2.y;
    float b = 2.0*(p1.y - p0.y);
    float c = p0.y - p.y;

    // Linear case
    if (abs(a) < 1e-6)
    {
        if (abs(b) < 1e-6)
            return 0;

        float t = -c / b;

        if (t < 0.0 || t > 1.0)
            return 0;

        float x = (1.0-t)*(1.0-t)*p0.x
                + 2.0*(1.0-t)*t*p1.x
                + t*t*p2.x;

        if (x < p.x)
            return 0;

        return b > 0.0 ? 1 : -1;
    }

    float d = b*b - 4.0*a*c;

    if (d < 0.0)
        return 0;

    float s = sqrt(d);
    float t0 = (-b - s) / (2.0*a);
    float t1 = (-b + s) / (2.0*a);

    // Test both intersections.
    if (t0 >= 0.0 && t0 <= 1.0)
    {
        float x = (1.0-t0)*(1.0-t0)*p0.x
                + 2.0*(1.0-t0)*t0*p1.x
                + t0*t0*p2.x;

        if (x >= p.x)
        {
            float dy = b + 2.0*a*t0;

            if (abs(dy) > 1e-6)
                return dy > 0.0 ? 1 : -1;
        }
    }

    if (t1 >= 0.0 && t1 <= 1.0)
    {
        float x = (1.0-t1)*(1.0-t1)*p0.x
                + 2.0*(1.0-t1)*t1*p1.x
                + t1*t1*p2.x;

        if (x >= p.x)
        {
            float dy = b + 2.0*a*t1;

            if (abs(dy) > 1e-6)
                return dy > 0.0 ? 1 : -1;
        }
    }

    return 0;
}

void main()
{
	frag_color = vec4(1.0);

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

			num_crossings += intersects(pos, p0, p1, p2);
		}

		contour_start = contour_end;
	}

	if (num_crossings % 2 != 0) {
		frag_color.rgb = vec3(0);
	}
}

