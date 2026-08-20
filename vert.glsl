#version 410 core

layout (location = 0) in vec2 pos;
layout (location = 1) in vec2 size;
layout (location = 2) in int point_offset;
layout (location = 3) in int contour_offset;
layout (location = 4) in int contour_count;

out vec2 frag_texcoord;
flat out int frag_contour_count;
flat out int frag_contour_offset;
flat out int frag_point_offset;

void main()
{
	float x_min = pos.x;
	float y_min = pos.y;
	float x_max = pos.x + size.x;
	float y_max = pos.y + size.y;

	vec4 corners[4];
	corners[0] = vec4(x_min, y_min, 0.0, 1.0);
	corners[1] = vec4(x_max, y_min, 0.0, 1.0);
	corners[2] = vec4(x_min, y_max, 0.0, 1.0);
	corners[3] = vec4(x_max, y_max, 0.0, 1.0);

	vec2 texcoords[4];
	texcoords[0] = vec2(0, 0);
	texcoords[1] = vec2(1, 0);
	texcoords[2] = vec2(0, 1);
	texcoords[3] = vec2(1, 1);

	gl_Position = corners[gl_VertexID];
	frag_texcoord = texcoords[gl_VertexID];
	frag_contour_count = contour_count;
	frag_contour_offset = contour_offset;
	frag_point_offset = point_offset;
}
