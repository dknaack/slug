#define GLFW_INCLUDE_NONE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <GLFW/glfw3.h>

#define GLAD_GL_IMPLEMENTATION
#include "glad.h"

#define TAG(a, b, c, d) ((a) << 24 | (b) << 16 | (c) << 8 | (d))
#define MAX(a, b) ((a) > (b)? (a) : (b))
#define MIN(a, b) ((a) < (b)? (a) : (b))

#define read_i64(r) ((int64_t)read_u64(r))
#define read_i32(r) ((int32_t)read_u32(r))
#define read_i16(r) ((int16_t)read_u16(r))
#define read_i8(r) ((int8_t)read_u8(r))

typedef struct {
	char *at;
	size_t length;
} str;

typedef struct {
	str input;
	size_t pos;
} reader;

typedef enum {
	OTF_TABLE_NONE,
	OTF_TABLE_CMAP,
	OTF_TABLE_GLYF,
	OTF_TABLE_HEAD,
	OTF_TABLE_LOCA,
	OTF_TABLE_MAXP,
	OTF_TABLE_COUNT
} otf_table_type;

typedef struct {
	float xx, xy;
	float yx, yy;
	float dx, dy;
} otf_transform;

typedef enum {
	OTF_ON_CURVE_POINT = 0x01,
	OTF_X_SHORT_VECTOR = 0x02,
	OTF_Y_SHORT_VECTOR = 0x04,
	OTF_REPEAT_FLAG    = 0x08,
	OTF_X_IS_SAME      = 0x10,
	OTF_Y_IS_SAME      = 0x20,
	OTF_OVERLAP_SIMPLE = 0x40,
	OTF_RESERVED       = 0x80,
} otf_simple_glyph_flags;

typedef enum {
	OTF_ARG_1_AND_2_ARE_WORDS     = (1 <<  0),
	OTF_ARGS_ARE_XY_VALUES        = (1 <<  1),
	OTF_ROUND_XY_TO_GRID          = (1 <<  2),
	OTF_WE_HAVE_A_SCALE           = (1 <<  3),
	OTF_MORE_COMPONENTS           = (1 <<  5),
	OTF_WE_HAVE_AN_X_AND_Y_SCALE  = (1 <<  6),
	OTF_WE_HAVE_A_TWO_BY_TWO      = (1 <<  7),
	OTF_WE_HAVE_INSTRUCTIONS      = (1 <<  8),
	OTF_USE_MY_METRICS            = (1 <<  9),
	OTF_OVERLAP_COMPOUND          = (1 << 10),
	OTF_SCALED_COMPONENT_OFFSET   = (1 << 11),
	OTF_UNSCALED_COMPONENT_OFFSET = (1 << 12),
} otf_composite_glyph_flags;

typedef struct {
	uint16_t units_per_em;
	uint16_t index_to_loc_format;
	uint32_t glyph_count;

	str tables[OTF_TABLE_COUNT];
} otf_font;

typedef struct {
	float x, y;
} point;

typedef struct {
	point *points;
	uint32_t *contours;
	uint32_t contour_count;
	uint32_t point_count;
	int32_t x_min, y_min;
	int32_t x_max, y_max;
} glyph;

static str read_file(char *path)
{
	str result = {0};

	FILE *file = fopen(path, "rb");
	if (file) {
		fseek(file, 0, SEEK_END);
		result.length = ftell(file);
		fseek(file, 0, SEEK_SET);

		result.at = malloc(result.length + 1);
		result.at[result.length] = '\0';
		fread(result.at, 1, result.length, file);
	}

	return result;
}

static uint64_t read_u64(reader *r)
{
	uint8_t *p = (uint8_t *)r->input.at;
	uint64_t result = 0;
	result |= (uint64_t)p[r->pos++] << 56ll;
	result |= (uint64_t)p[r->pos++] << 48ll;
	result |= (uint64_t)p[r->pos++] << 40ll;
	result |= (uint64_t)p[r->pos++] << 32ll;
	result |= (uint64_t)p[r->pos++] << 24ll;
	result |= (uint64_t)p[r->pos++] << 16ll;
	result |= (uint64_t)p[r->pos++] <<  8ll;
	result |= (uint64_t)p[r->pos++] <<  0ll;
	return result;
}

static uint32_t read_u32(reader *r)
{
	uint8_t *p = (uint8_t *)r->input.at;
	uint32_t result = 0;
	result |= (uint32_t)p[r->pos++] << 24;
	result |= (uint32_t)p[r->pos++] << 16;
	result |= (uint32_t)p[r->pos++] <<  8;
	result |= (uint32_t)p[r->pos++] <<  0;
	return result;
}

static uint16_t read_u16(reader *r)
{
	uint8_t *p = (uint8_t *)r->input.at;
	uint16_t result = 0;
	result |= (uint16_t)p[r->pos++] << 8;
	result |= (uint16_t)p[r->pos++] << 0;
	return result;
}

static uint16_t read_u8(reader *r)
{
	uint8_t *p = (uint8_t *)r->input.at;
	uint16_t result = p[r->pos++];
	return result;
}

static float fixed_2_14(int16_t value)
{
	return (float)value / 16384.0f;
}

static uint32_t get_glyph_index(otf_font *font, uint32_t codepoint)
{
	uint32_t result = 0;
	reader cmap = {font->tables[OTF_TABLE_CMAP]};

	uint32_t offset = 0;
	uint16_t _version = read_u16(&cmap);
	uint16_t num_tables = read_u16(&cmap);

	for (uint16_t i = 0; i < num_tables; i++) {
		uint16_t _platform_id = read_u16(&cmap);
		uint16_t _encoding_id = read_u16(&cmap);
		uint32_t subtable_offset = read_u32(&cmap);

		size_t orig_pos = cmap.pos;
		cmap.pos = subtable_offset;

		uint16_t format = read_u16(&cmap);
		uint16_t length = read_u16(&cmap);
		uint16_t language = read_u16(&cmap);
		uint16_t segment_count_twice = read_u16(&cmap);
		uint16_t _search_range = read_u16(&cmap);
		uint16_t _entry_selector = read_u16(&cmap);
		uint16_t _range_shift = read_u16(&cmap);

		int found_segment = 0;
		int32_t segment_index = 0;
		uint16_t segment_end = 0;
		uint16_t segment_count = segment_count_twice / 2;
		for (uint16_t i = 0; i < segment_count; i++) {
			uint16_t end_code = read_u16(&cmap);
			if (!found_segment && codepoint <= end_code) {
				segment_index = i;
				segment_end = end_code;
				found_segment = 1;
			}
		}

		cmap.pos += 2; // padding byte

		uint16_t segment_start = 0;
		for (uint16_t i = 0; i < segment_count; i++) {
			uint16_t start_code = read_u16(&cmap);
			if (i == segment_index) {
				segment_start = start_code;
				found_segment = (start_code <= codepoint);
			}
		}

		int16_t segment_delta = 0;
		for (uint16_t i = 0; i < segment_count; i++) {
			int16_t id_delta = read_i16(&cmap);
			if (i == segment_index) {
				segment_delta = id_delta;
			}
		}

		uint16_t segment_range_offset = 0;
		size_t address = 0;
		for (uint16_t i = 0; i < segment_count; i++) {
			uint16_t id_range_offset = read_u16(&cmap);
			if (i == segment_index) {
				segment_range_offset = id_range_offset;
				address = cmap.pos;
			}
		}

		if (found_segment) {
			if (segment_range_offset == 0) {
				result = (codepoint + segment_delta) & 0xFFFF;
			} else {
				cmap.pos = address + segment_range_offset + 2 * (codepoint - segment_start);
				result = read_u16(&cmap);
			}
		}

		cmap.pos = orig_pos;
	}

	return result;
}

static glyph convert_glyph(otf_font *font, uint32_t glyph_index)
{
	glyph result = {0};

	// Get the glyph offset
	reader loca;
	loca.input = font->tables[OTF_TABLE_LOCA];
	loca.pos = glyph_index;

	uint32_t offset = 0;
	if (font->index_to_loc_format == 0) {
		loca.pos = sizeof(uint16_t) * glyph_index;
		uint16_t offset_divided_by_two = read_u16(&loca);
		offset = offset_divided_by_two * 2;
	} else {
		loca.pos = sizeof(uint32_t) * glyph_index;
		offset = read_u32(&loca);
	}

	// Convert the glyph
	reader glyf = {0};
	glyf.input = font->tables[OTF_TABLE_GLYF];
	glyf.pos = offset;

	int16_t contour_count = read_u16(&glyf);
	if (contour_count >= 0) {
		// Simple glyph
		result.x_min = read_i16(&glyf);
		result.y_min = read_i16(&glyf);
		result.x_max = read_i16(&glyf);
		result.y_max = read_i16(&glyf);

		uint16_t *end_points = calloc(contour_count, sizeof(*end_points));
		for (uint16_t i = 0; i < contour_count; i++) {
			end_points[i] = read_u16(&glyf);
		}

		uint16_t point_count = end_points[contour_count - 1] + 1;

		// Ignore any instructions
		uint16_t instruction_length = read_u16(&glyf);
		glyf.pos += instruction_length;

		// Decode the flags
		uint8_t repeat_count = 0;
		uint8_t *flags = calloc(point_count, sizeof(*flags));
		for (uint16_t i = 0; i < point_count; i++) {
			if (repeat_count > 0) {
				flags[i] = flags[i - 1];
				repeat_count -= 1;
			} else {
				flags[i] = read_u8(&glyf);
				if (flags[i] & OTF_REPEAT_FLAG) {
					repeat_count = read_u8(&glyf);
				}
			}
		}

		point *points = calloc(point_count, sizeof(*points));

		// Decode x-coordinates
		int16_t x = 0;
		for (uint16_t i = 0; i < point_count; i++) {
			uint8_t flag = flags[i];
			if (flag & OTF_X_SHORT_VECTOR) {
				uint8_t dx = read_u8(&glyf);
				if (flag & OTF_X_IS_SAME) {
					x += dx;
				} else {
					x -= dx;
				}
			} else if (!(flag & OTF_X_IS_SAME)) {
				x += read_i16(&glyf);
			}

			points[i].x = (float)(x - result.x_min) / (float)(result.x_max - result.x_min);
		}

		// Decode y-coordinates
		int16_t y = 0;
		for (uint16_t i = 0; i < point_count; i++) {
			uint8_t flag = flags[i];

			if (flag & OTF_Y_SHORT_VECTOR) {
				uint8_t dy = read_u8(&glyf);

				if (flag & OTF_Y_IS_SAME) {
					y += dy;
				} else {
					y -= dy;
				}
			} else if (!(flag & OTF_Y_IS_SAME)) {
				y += read_i16(&glyf);
			}

			points[i].y = (float)(y - result.y_min) / (float)(result.y_max - result.y_min);
		}

		// Insert midpoints
		result.points = calloc(2 * point_count, sizeof(*result.points));
		result.contours = calloc(contour_count, sizeof(*result.contours));
		result.contour_count = contour_count;
		result.point_count = 0;

		uint32_t contour_start = 0;
		for (uint16_t j = 0; j < contour_count; j++) {
			uint16_t contour_end = end_points[j];
			point prev_point = points[contour_end];
			uint8_t prev_flag = flags[contour_end];

			// Ensure that first point is always on the curve
			if (prev_flag & OTF_ON_CURVE_POINT) {
				result.points[result.point_count++] = prev_point;
			}

			for (uint16_t i = contour_start; i <= contour_end; i++) {
				point curr_point = points[i];
				uint8_t curr_flag = flags[i];
				if ((curr_flag & OTF_ON_CURVE_POINT) == (prev_flag & OTF_ON_CURVE_POINT)) {
					point midpoint = {0};
					midpoint.x = (prev_point.x + curr_point.x) / 2;
					midpoint.y = (prev_point.y + curr_point.y) / 2;
					result.points[result.point_count++] = midpoint;
				}

				if (!(i == contour_end && (curr_flag & OTF_ON_CURVE_POINT))) {
					assert(result.point_count < 2 * point_count);
					result.points[result.point_count++] = curr_point;
				}

				prev_point = curr_point;
				prev_flag = curr_flag;
			}

			contour_start = contour_end + 1;
			result.contours[j] = result.point_count;
		}

		assert(result.point_count % 2 == 0);
		result.points = realloc(result.points, result.point_count * sizeof(*result.points));
	} else {
		// Composite glyph
		result.x_min = read_i16(&glyf);
		result.y_min = read_i16(&glyf);
		result.x_max = read_i16(&glyf);
		result.y_max = read_i16(&glyf);

		uint16_t flags = 0;
		do {
			flags = read_u16(&glyf);
			uint16_t component_index = read_u16(&glyf);
			glyph component = convert_glyph(font, component_index);

			int16_t arg1, arg2;
			if (flags & OTF_ARG_1_AND_2_ARE_WORDS) {
				arg1 = read_i16(&glyf);
				arg2 = read_i16(&glyf);
			} else {
				arg1 = read_i8(&glyf);
				arg2 = read_i8(&glyf);
			}

			otf_transform t = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
			if (flags & OTF_WE_HAVE_A_SCALE) {
				t.xx = t.yy = fixed_2_14(read_i16(&glyf));
			} else if (flags & OTF_WE_HAVE_AN_X_AND_Y_SCALE) {
				t.xx = fixed_2_14(read_i16(&glyf));
				t.yy = fixed_2_14(read_i16(&glyf));
			} else if (flags & OTF_WE_HAVE_A_TWO_BY_TWO) {
				t.xx = fixed_2_14(read_i16(&glyf));
				t.xy = fixed_2_14(read_i16(&glyf));
				t.yx = fixed_2_14(read_i16(&glyf));
				t.yy = fixed_2_14(read_i16(&glyf));
			}

			float dx = 0;
			float dy = 0;
			if (flags & OTF_ARGS_ARE_XY_VALUES) {
				t.dx = arg1;
				t.dy = arg2;
			} else {
				point p = component.points[arg1];
				point q = component.points[arg2];

				float px = p.x * (result.x_max - result.x_min) + result.x_min;
				float py = p.y * (result.y_max - result.y_min) + result.y_min;

				float qx = q.x * (component.x_max - component.x_min) + component.x_min;
				float qy = q.y * (component.y_max - component.y_min) + component.y_min;

				t.dx = px - (t.xx * qx + t.xy * qy);
				t.dy = py - (t.yx * qx + t.yy * qy);
			}

			// Append the glyph
			uint32_t point_count = result.point_count + component.point_count;
			uint32_t contour_count = result.contour_count + component.contour_count;
			result.points = realloc(result.points, point_count * sizeof(*result.points));
			result.contours = realloc(result.contours, contour_count * sizeof(*result.contours));

			uint32_t point_offset = result.point_count;
			uint32_t contour_offset = result.contour_count;
			for (uint32_t i = 0; i < component.point_count; i++) {
				float x = component.points[i].x * (component.x_max - component.x_min) + component.x_min;
				float y = component.points[i].y * (component.y_max - component.y_min) + component.y_min;

				x = t.xx * x + t.xy * y + t.dx;
				y = t.yx * x + t.yy * y + t.dy;

				result.points[point_offset + i].x = (float)(x - result.x_min) / (float)(result.x_max - result.x_min);
				result.points[point_offset + i].y = (float)(y - result.y_min) / (float)(result.y_max - result.y_min);
			}

			for (uint32_t i = 0; i < component.contour_count; i++) {
				result.contours[contour_offset + i] = point_offset + component.contours[i];
			}

			result.point_count += component.point_count;
			result.contour_count += component.contour_count;
		} while (flags & OTF_MORE_COMPONENTS);
	}

	return result;
}

static GLuint create_shader(const char *src, GLenum type)
{
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &src, NULL);
	glCompileShader(shader);

	int success = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if (!success) {
		char info_log[1024] = {0};
		glGetShaderInfoLog(shader, sizeof(info_log) - 1, NULL, info_log);
		printf("Failed to compile shader: %s\n", info_log);
		return 0;
	}

	return shader;
}

int main(void)
{
	if (!glfwInit()) {
		return -1;
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	GLFWwindow *window = glfwCreateWindow(640, 480, "Hello World", NULL, NULL);
	if (!window) {
		return -1;
	}

	glfwMakeContextCurrent(window);

	int version = gladLoadGL(glfwGetProcAddress);
	if (version == 0) {
		return -1;
	}

	otf_font font = {0};
	glyph *glyphs = NULL;
	uint32_t *offsets = NULL;
	uint32_t point_count = 0;
	uint32_t max_point_count = 0;
	uint32_t contour_count = 0;
	{
		reader r = {0};
		r.input = read_file("fonts/OpenSans-Regular.ttf");
		if (!r.input.at) {
			return -1;
		}

		uint32_t _version = read_u32(&r);
		uint16_t num_tables = read_u16(&r);
		uint16_t _search_range = read_u16(&r);
		uint16_t _entry_selector = read_u16(&r);
		uint16_t _range_shift = read_u16(&r);

		for (uint16_t i = 0; i < num_tables; i++) {
			uint32_t tag = read_u32(&r);
			uint32_t _checksum = read_u32(&r);
			uint32_t offset = read_u32(&r);
			uint32_t length = read_u32(&r);

			switch (tag) {
			case TAG('c', 'm', 'a', 'p'):
				tag = OTF_TABLE_CMAP;
				break;
			case TAG('g', 'l', 'y', 'f'):
				tag = OTF_TABLE_GLYF;
				break;
			case TAG('h', 'e', 'a', 'd'):
				tag = OTF_TABLE_HEAD;
				break;
			case TAG('l', 'o', 'c', 'a'):
				tag = OTF_TABLE_LOCA;
				break;
			case TAG('m', 'a', 'x', 'p'):
				tag = OTF_TABLE_MAXP;
				break;
			default:
				tag = OTF_TABLE_NONE;
			}

			if (tag != 0) {
				font.tables[tag] = r.input;
				font.tables[tag].at += offset;
				font.tables[tag].length = length;
			}
		}

		// read the head table
		{
			reader head = {font.tables[OTF_TABLE_HEAD]};
			uint16_t _major_version = read_u16(&head);
			uint16_t _minor_version = read_u16(&head);
			uint32_t _font_revision = read_u32(&head);
			uint32_t _checksum_adjustment = read_u32(&head);
			uint32_t _magic_number = read_u32(&head);
			uint16_t _flags = read_u16(&head);
			font.units_per_em = read_u16(&head);
			int64_t _created = read_u64(&head);
			int64_t _modified = read_u64(&head);
			int16_t _x_min = read_u16(&head);
			int16_t _y_min = read_u16(&head);
			int16_t _x_max = read_u16(&head);
			int16_t _y_max = read_u16(&head);
			uint16_t _mac_style = read_u16(&head);
			uint16_t _lowest_rec_ppem = read_u16(&head);
			int16_t _font_direction_hint = read_u16(&head);
			font.index_to_loc_format = read_u16(&head);
			int16_t _glyph_data_format = read_u16(&head);
		}

		// read the maxp table
		{
			reader maxp = {font.tables[OTF_TABLE_MAXP]};
			uint32_t _version = read_u32(&maxp);
			font.glyph_count = read_u16(&maxp);
		}

		glyphs = calloc(font.glyph_count, sizeof(*glyphs));
		offsets = calloc(2 * font.glyph_count, sizeof(*offsets));
		for (uint32_t i = 0; i < font.glyph_count; i++) {
			glyphs[i] = convert_glyph(&font, i);
			offsets[2 * i + 0] = point_count;
			offsets[2 * i + 1] = contour_count;
			point_count += glyphs[i].point_count;
			max_point_count = MAX(max_point_count, glyphs[i].point_count);
			contour_count += glyphs[i].contour_count;
		}
	}

	str vertex_shader_source = read_file("vert.glsl");
	str fragment_shader_source = read_file("frag.glsl");
	GLuint vertex_shader = create_shader(vertex_shader_source.at, GL_VERTEX_SHADER);
	GLuint fragment_shader = create_shader(fragment_shader_source.at, GL_FRAGMENT_SHADER);

	GLuint program = glCreateProgram();
	glAttachShader(program, vertex_shader);
	glAttachShader(program, fragment_shader);
	glLinkProgram(program);

	int success = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &success);
	if (!success) {
		char info_log[1024] = {0};
		glGetProgramInfoLog(program, sizeof(info_log) - 1, NULL, info_log);
		printf("Failed to compile shader: %s\n", info_log);
		return -1;
	}

	glUseProgram(program);
	glUniform1i(glGetUniformLocation(program, "point_data"), 0);
	glUniform1i(glGetUniformLocation(program, "contour_data"), 1);

	GLuint point_buffer;
	glGenBuffers(1, &point_buffer);
	glBindBuffer(GL_TEXTURE_BUFFER, point_buffer);
	glBufferData(GL_TEXTURE_BUFFER, point_count * sizeof(point), NULL, GL_STATIC_DRAW);
	for (uint32_t i = 0; i < font.glyph_count; i++) {
		glBufferSubData(GL_TEXTURE_BUFFER,
			offsets[2 * i] * sizeof(point),
			glyphs[i].point_count * sizeof(point),
			glyphs[i].points);
	}

	GLuint point_texture;
	glGenTextures(1, &point_texture);
	glBindTexture(GL_TEXTURE_BUFFER, point_texture);
	glTexBuffer(GL_TEXTURE_BUFFER, GL_RG32F, point_buffer);

	GLuint contour_buffer;
	glGenBuffers(1, &contour_buffer);
	glBindBuffer(GL_TEXTURE_BUFFER, contour_buffer);
	glBufferData(GL_TEXTURE_BUFFER, contour_count * sizeof(uint32_t), NULL, GL_STATIC_DRAW);
	for (uint32_t i = 0; i < font.glyph_count; i++) {
		glBufferSubData(GL_TEXTURE_BUFFER,
			offsets[2 * i + 1] * sizeof(uint32_t),
			glyphs[i].contour_count * sizeof(uint32_t),
			glyphs[i].contours);
	}

	GLuint contour_texture;
	glGenTextures(1, &contour_texture);
	glBindTexture(GL_TEXTURE_BUFFER, contour_texture);
	glTexBuffer(GL_TEXTURE_BUFFER, GL_R32I, contour_buffer);

	GLuint vertex_array = 0;
	glGenVertexArrays(1, &vertex_array);
	glBindVertexArray(vertex_array);

	uint32_t glyph_index = get_glyph_index(&font, 'a');
	glyph glyph = glyphs[glyph_index];

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_BUFFER, point_texture);

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_BUFFER, contour_texture);

	while (!glfwWindowShouldClose(window)) {
		int viewport_width, viewport_height;
		glfwGetFramebufferSize(window, &viewport_width, &viewport_height);
		glViewport(0, 0, viewport_width, viewport_height);
		glClear(GL_COLOR_BUFFER_BIT);

		float size = 1024.f;
		float width = (float)(glyph.x_max - glyph.x_min) / font.units_per_em;
		float height = (float)(glyph.y_max - glyph.y_min) / font.units_per_em;
		width *= size / viewport_width;
		height *= size / viewport_height;

		glUseProgram(program);
		glUniform1i(glGetUniformLocation(program, "point_data"), 0);
		glUniform1i(glGetUniformLocation(program, "contour_data"), 1);
		glUniform1f(glGetUniformLocation(program, "x_min"), -width/2);
		glUniform1f(glGetUniformLocation(program, "y_min"), -height/2);
		glUniform1f(glGetUniformLocation(program, "x_max"), width/2);
		glUniform1f(glGetUniformLocation(program, "y_max"), height/2);
		glUniform1i(glGetUniformLocation(program, "point_offset"), offsets[2 * glyph_index + 0]);
		glUniform1i(glGetUniformLocation(program, "contour_offset"), offsets[2 * glyph_index + 1]);

		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	glfwTerminate();
	return 0;
}
