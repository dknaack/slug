#define GLFW_INCLUDE_NONE
#include <assert.h>
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <CoreText/CoreText.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreFoundation/CoreFoundation.h>

#define GLAD_GL_IMPLEMENTATION
#include "glad.h"

#define TAG(a, b, c, d) ((a) << 24 | (b) << 16 | (c) << 8 | (d))
#define MAX(a, b) ((a) > (b)? (a) : (b))
#define MIN(a, b) ((a) < (b)? (a) : (b))
#define LENGTH(x) (sizeof(x)/sizeof((x)[0]))

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
	OTF_TABLE_CFF,
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

#define MAX_POINT_COUNT (256 * 256)
#define MAX_CONTOUR_COUNT (64 * 64)

typedef struct {
	float pos[2];
	float size[2];
	uint32_t point_offset;
	uint32_t contour_offset;
	uint32_t contour_count;
} glyph;

typedef struct {
	point points[MAX_POINT_COUNT];
	uint32_t contours[MAX_CONTOUR_COUNT];
	glyph *glyphs;

	uint32_t point_count;
	uint32_t contour_count;
	uint32_t glyph_count;
} glyph_storage;

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

static uint8_t read_u8(reader *r)
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

static glyph convert_glyph(glyph_storage *out, otf_font *font, uint32_t glyph_index)
{
	glyph result = {0};

	// Get the glyph offset
	reader loca;
	loca.input = font->tables[OTF_TABLE_LOCA];
	loca.pos = glyph_index;
	assert(glyph_index < font->glyph_count);

	uint32_t offset, next_offset;
	if (font->index_to_loc_format == 0) {
		loca.pos = sizeof(uint16_t) * glyph_index;
		offset = 2 * read_u16(&loca);
		next_offset = 2 * read_u16(&loca);
	} else {
		loca.pos = sizeof(uint32_t) * glyph_index;
		offset = read_u32(&loca);
		next_offset = read_u32(&loca);
	}

	// Convert the glyph
	reader glyf = {0};
	glyf.input = font->tables[OTF_TABLE_GLYF];
	glyf.pos = offset;

	int16_t contour_count = 0;
	if (offset != next_offset) {
		contour_count = read_u16(&glyf);
	}

	if (contour_count > 0) {
		// Simple glyph
		int16_t x_min = read_i16(&glyf);
		int16_t y_min = read_i16(&glyf);
		int16_t x_max = read_i16(&glyf);
		int16_t y_max = read_i16(&glyf);

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

		point points[1024] = {0};

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

			points[i].x = (float)(x - x_min) / (float)(x_max - x_min);
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

			points[i].y = (float)(y - y_min) / (float)(y_max - y_min);
		}

		result.pos[0] = x_min;
		result.pos[1] = y_min;
		result.size[0] = x_max - x_min;
		result.size[1] = y_max - y_min;
		result.contour_offset = out->contour_count;
		result.contour_count = contour_count;
		result.point_offset = out->point_count;

		// Insert midpoints
		point *out_points = out->points + out->point_count;
		uint32_t *out_contours = out->contours + out->contour_count;
		uint32_t out_point_count = 0;
		uint32_t contour_start = 0;

		for (uint16_t j = 0; j < contour_count; j++) {
			uint16_t contour_end = end_points[j];
			point prev_point = points[contour_end];
			uint8_t prev_flag = flags[contour_end];

			// Ensure that first point is always on the curve
			if (prev_flag & OTF_ON_CURVE_POINT) {
				out_points[out_point_count++] = prev_point;
				assert(out->point_count + out_point_count <= MAX_POINT_COUNT);
			}

			for (uint16_t i = contour_start; i <= contour_end; i++) {
				point curr_point = points[i];
				uint8_t curr_flag = flags[i];
				if ((curr_flag & OTF_ON_CURVE_POINT) == (prev_flag & OTF_ON_CURVE_POINT)) {
					point midpoint = {0};
					midpoint.x = (prev_point.x + curr_point.x) / 2;
					midpoint.y = (prev_point.y + curr_point.y) / 2;
					out_points[out_point_count++] = midpoint;
					assert(out->point_count + out_point_count <= MAX_POINT_COUNT);
				}

				if (!(i == contour_end && (curr_flag & OTF_ON_CURVE_POINT))) {
					assert(out_point_count < 2 * point_count);
					out_points[out_point_count++] = curr_point;
					assert(out->point_count + out_point_count <= MAX_POINT_COUNT);
				}

				prev_point = curr_point;
				prev_flag = curr_flag;
			}

			contour_start = contour_end + 1;
			out_contours[j] = out_point_count;
		}

		out->contour_count += contour_count;
		out->point_count += out_point_count;
		assert(out_point_count % 2 == 0);
	} else if (contour_count < 0) {
		// Composite glyph
		int16_t x_min = read_i16(&glyf);
		int16_t y_min = read_i16(&glyf);
		int16_t x_max = read_i16(&glyf);
		int16_t y_max = read_i16(&glyf);

		result.pos[0] = x_min;
		result.pos[1] = y_min;
		result.size[0] = x_max - x_min;
		result.size[1] = y_max - y_min;

		result.point_offset = out->point_count;
		result.contour_offset = out->contour_count;

		uint16_t flags = 0;
		do {
			flags = read_u16(&glyf);
			uint16_t component_index = read_u16(&glyf);

			/*
			 * Decode the component directly into the output buffers.
			 * Save where it starts so we can transform it below.
			 */
			uint32_t point_offset = out->point_count;
			uint32_t contour_offset = out->contour_count;

			glyph component = convert_glyph(out, font, component_index);

			int16_t arg1, arg2;
			if (flags & OTF_ARG_1_AND_2_ARE_WORDS) {
				arg1 = read_i16(&glyf);
				arg2 = read_i16(&glyf);
			} else {
				arg1 = read_i8(&glyf);
				arg2 = read_i8(&glyf);
			}

			otf_transform t = {
				1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f
			};

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

			if (flags & OTF_ARGS_ARE_XY_VALUES) {
				t.dx = arg1;
				t.dy = arg2;
			} else {
				point p = out->points[point_offset + arg1];
				point q = out->points[point_offset + arg2];

				float px = p.x * component.size[0] + component.pos[0];
				float py = p.y * component.size[1] + component.pos[1];

				float qx = q.x * component.size[0] + component.pos[0];
				float qy = q.y * component.size[1] + component.pos[1];

				t.dx = px - (t.xx * qx + t.xy * qy);
				t.dy = py - (t.yx * qx + t.yy * qy);
			}

			// Transform the component in place
			uint32_t component_point_count = out->point_count - point_offset;
			for (uint32_t i = 0; i < component_point_count; i++) {
				point *p = &out->points[point_offset + i];

				float x = p->x * component.size[0] + component.pos[0];
				float y = p->y * component.size[1] + component.pos[1];

				x = t.xx * x + t.xy * y + t.dx;
				y = t.yx * x + t.yy * y + t.dy;

				p->x = (x - x_min) / (float)(x_max - x_min);
				p->y = (y - y_min) / (float)(y_max - y_min);
			}

			// Adjust contour offsets in place.
			for (uint32_t i = 0; i < component.contour_count; i++) {
				out->contours[contour_offset + i] += point_offset;
			}
		} while (flags & OTF_MORE_COMPONENTS);

		result.contour_count = out->contour_count - result.contour_offset;
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
		fprintf(stderr, "Failed to compile shader: %s\n", info_log);
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

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	reader r = {0};
	r.input = read_file("fonts/latinmodern-math.otf");
	if (!r.input.at) {
		return -1;
	}

	otf_font font = {0};
	glyph *instances = NULL;
	glyph_storage storage = {0};
	GLuint point_texture, contour_texture;
	{
		uint32_t _version = read_u32(&r);
		uint16_t num_tables = read_u16(&r);
		uint16_t _search_range = read_u16(&r);
		uint16_t _entry_selector = read_u16(&r);
		uint16_t _range_shift = read_u16(&r);

		for (uint16_t i = 0; i < num_tables; i++) {
			printf("%.4s\n", r.input.at + r.pos);
			uint32_t tag = read_u32(&r);
			uint32_t _checksum = read_u32(&r);
			uint32_t offset = read_u32(&r);
			uint32_t length = read_u32(&r);

			switch (tag) {
			case TAG('C', 'F', 'F', ' '):
				tag = OTF_TABLE_CFF;
				break;
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

		// read the maxp table
		reader maxp = {font.tables[OTF_TABLE_MAXP]};
		uint32_t _maxp_version = read_u32(&maxp);
		font.glyph_count = read_u16(&maxp);

		// convert the glyphs
		storage.glyphs = calloc(font.glyph_count, sizeof(*storage.glyphs));
		for (uint32_t i = 0; i < font.glyph_count; i++) {
			storage.glyphs[i] = convert_glyph(&storage, &font, i);
			storage.glyphs[i].pos[0] /= font.units_per_em;
			storage.glyphs[i].pos[1] /= font.units_per_em;
			storage.glyphs[i].size[0] /= font.units_per_em;
			storage.glyphs[i].size[1] /= font.units_per_em;
		}

		glGenTextures(1, &point_texture);
		glBindTexture(GL_TEXTURE_2D, point_texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RG, 256, 256, 0, GL_RG, GL_FLOAT, storage.points);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

		glGenTextures(1, &contour_texture);
		glBindTexture(GL_TEXTURE_2D, contour_texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_R32I, 64, 64, 0, GL_RED_INTEGER, GL_INT, storage.contours);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	}

	GLuint vertex_array;
	glGenVertexArrays(1, &vertex_array);
	glBindVertexArray(vertex_array);

	GLuint instance_buffer;
	glGenBuffers(1, &instance_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, instance_buffer);
	glBufferData(GL_ARRAY_BUFFER, 1024 * sizeof(glyph), NULL, GL_DYNAMIC_DRAW);

	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glyph), (void *)offsetof(glyph, pos));
	glEnableVertexAttribArray(0);
	glVertexAttribDivisor(0, 1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(glyph), (void *)offsetof(glyph, size));
	glEnableVertexAttribArray(1);
	glVertexAttribDivisor(1, 1);
	glVertexAttribIPointer(2, 1, GL_UNSIGNED_INT, sizeof(glyph), (void *)offsetof(glyph, point_offset));
	glEnableVertexAttribArray(2);
	glVertexAttribDivisor(2, 1);
	glVertexAttribIPointer(3, 1, GL_UNSIGNED_INT, sizeof(glyph), (void *)offsetof(glyph, contour_offset));
	glEnableVertexAttribArray(3);
	glVertexAttribDivisor(3, 1);
	glVertexAttribIPointer(4, 1, GL_UNSIGNED_INT, sizeof(glyph), (void *)offsetof(glyph, contour_count));
	glEnableVertexAttribArray(4);
	glVertexAttribDivisor(4, 1);

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
		fprintf(stderr, "Failed to compile shader: %s\n", info_log);
		return -1;
	}

	glUseProgram(program);
	glUniform1i(glGetUniformLocation(program, "point_data"), 0);
	glUniform1i(glGetUniformLocation(program, "contour_data"), 1);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, point_texture);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, contour_texture);

	float font_size = 128.0f;
	CFDataRef data = CFDataCreate(NULL, (uint8_t *)r.input.at, r.input.length);
	CTFontDescriptorRef descriptor = CTFontManagerCreateFontDescriptorFromData(data);
	CTFontRef font_ref = CTFontCreateWithFontDescriptor(descriptor, font_size, NULL);

	CFStringRef text = CFSTR("Hello, world!");
	CFMutableAttributedStringRef string =
		CFAttributedStringCreateMutable(NULL, 0);
	CFAttributedStringReplaceString(string, CFRangeMake(0, 0), text);
	CFAttributedStringSetAttribute(string,
		CFRangeMake(0, CFStringGetLength(text)),
		kCTFontAttributeName, font_ref);

	CTLineRef line = CTLineCreateWithAttributedString(string);
	CFArrayRef runs = CTLineGetGlyphRuns(line);

	while (!glfwWindowShouldClose(window)) {
		int viewport_width, viewport_height;
		glfwGetFramebufferSize(window, &viewport_width, &viewport_height);
		glViewport(0, 0, viewport_width, viewport_height);
		glClear(GL_COLOR_BUFFER_BIT);

		float pos[2] = {0};
		uint32_t instance_count = 0;
		glyph instance_data[64] = {0};
		for (CFIndex i = 0; i < CFArrayGetCount(runs); i++) {
			CTRunRef run = (CTRunRef)CFArrayGetValueAtIndex(runs, i);
			CFIndex glyph_count = CTRunGetGlyphCount(run);
			const CGGlyph *glyphs = CTRunGetGlyphsPtr(run);
			const CGPoint *positions = CTRunGetPositionsPtr(run);
			for (CFIndex j = 0; j < glyph_count; j++) {
				CGGlyph glyph_index = glyphs[j];
				glyph *inst = &instance_data[instance_count++];
				*inst = storage.glyphs[glyph_index];

				inst->pos[0] = (inst->pos[0] * font_size + positions[j].x) / viewport_width;
				inst->pos[1] = (inst->pos[1] * font_size + positions[j].y) / viewport_height;
				inst->size[0] *= font_size / viewport_width;
				inst->size[1] *= font_size / viewport_height;
			}
		}

		glBufferSubData(GL_ARRAY_BUFFER, 0, instance_count * sizeof(*instance_data), instance_data);

		glUseProgram(program);
		glUniform1i(glGetUniformLocation(program, "point_data"), 0);
		glUniform1i(glGetUniformLocation(program, "contour_data"), 1);

		glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, instance_count);

		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	glfwTerminate();
	return 0;
}
