#version 410 core

out vec2 pos;

uniform float x_min;
uniform float y_min;
uniform float x_max;
uniform float y_max;

void main()
{
	vec4 corners[4];
	corners[0] = vec4(x_min, y_min, 0.0, 1.0);
	corners[1] = vec4(x_max, y_min, 0.0, 1.0);
	corners[2] = vec4(x_min, y_max, 0.0, 1.0);
	corners[3] = vec4(x_max, y_max, 0.0, 1.0);

	vec2 tex_coords[4];
	tex_coords[0] = vec2(0, 0);
	tex_coords[1] = vec2(1, 0);
	tex_coords[2] = vec2(0, 1);
	tex_coords[3] = vec2(1, 1);

	gl_Position = corners[gl_VertexID];
	pos = tex_coords[gl_VertexID];
}
