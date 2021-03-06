// Pithesiser - a software synthesiser for Raspberry Pi
// Copyright (C) 2015 Nicholas Tuckett
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

/*
 * gfx_envelope_render.c
 *
 *  Created on: 19 Jan 2013
 *      Author: ntuckett
 */

#include "gfx_envelope_render.h"
#include <stdlib.h>
#include "VG/openvg.h"
#include "gfx_event.h"
#include "gfx_event_types.h"
#include "gfx_font.h"
#include "gfx_font_resources.h"

typedef struct envelope_renderer_state_t
{
	VGPaint line_paint;
	VGPaint text_paint;
	VGPath path;
	int display_update;
} envelope_renderer_state_t;

typedef struct envelope_render_internal_t
{
	envelope_renderer_t	definition;
	envelope_renderer_state_t state;
} envelope_render_internal_t;

static void initialise_renderer(envelope_render_internal_t *renderer)
{
	renderer->state.display_update = 1;
	renderer->state.line_paint = vgCreatePaint();
	vgSetParameteri(renderer->state.line_paint, VG_PAINT_TYPE, VG_PAINT_TYPE_COLOR);
	vgSetParameterfv(renderer->state.line_paint, VG_PAINT_COLOR, 4, renderer->definition.line_colour);
	renderer->state.text_paint = vgCreatePaint();
	vgSetParameteri(renderer->state.text_paint, VG_PAINT_TYPE, VG_PAINT_TYPE_COLOR);
	vgSetParameterfv(renderer->state.text_paint, VG_PAINT_COLOR, 4, renderer->definition.text_colour);
	renderer->state.path = vgCreatePath(VG_PATH_FORMAT_STANDARD, VG_PATH_DATATYPE_S_16, 1.0f, 0.0f, 0, 0, VG_PATH_CAPABILITY_APPEND_TO);
}

static void deinitialise_renderer(envelope_render_internal_t *renderer)
{
	vgDestroyPath(renderer->state.path);
	vgDestroyPaint(renderer->state.line_paint);
	vgDestroyPaint(renderer->state.text_paint);
}

static VGubyte commands[ENVELOPE_STAGES_MAX * 2];
static VGshort coords[ENVELOPE_STAGES_MAX * 2 * 2];
#define HELD_DURATION_LENGTH	1280

static void update_display(envelope_render_internal_t *renderer)
{
	// Calculate scale
	envelope_t *envelope = renderer->definition.envelope;
	int32_t total_duration = 0;

	envelope_stage_t *stage = envelope->stages;
	for (int i = 0; i < envelope->stage_count; i++, stage++)
	{
		if (stage->duration == DURATION_HELD)
		{
			total_duration += HELD_DURATION_LENGTH;
		}
		else
		{
			total_duration += stage->duration;
		}
	}

	float horizontal_scale = (float)(renderer->definition.width - 1) / (float) total_duration;
	float vertical_scale = (float)(renderer->definition.height - 1) / (float)envelope->peak;

	// Build path segments from envelope stages
	int command_index = 0;
	int coord_index = 0;

	int32_t	last_level = -1;
	int32_t	elapsed_duration = 0;

	stage = envelope->stages;
	for (int i = 0; i < envelope->stage_count; i++, stage++)
	{
		if (last_level != stage->start_level && stage->start_level != LEVEL_CURRENT)
		{
			commands[command_index++] = VG_MOVE_TO_ABS;
			coords[coord_index++] = (float)elapsed_duration * horizontal_scale;
			coords[coord_index++] = (float)stage->start_level * vertical_scale;
		}

		last_level = stage->end_level;
		if (stage->duration == DURATION_HELD)
		{
			elapsed_duration += HELD_DURATION_LENGTH;
		}
		else
		{
			elapsed_duration += stage->duration;
		}

		commands[command_index++] = VG_LINE_TO_ABS;
		coords[coord_index++] = (float)elapsed_duration * horizontal_scale;
		coords[coord_index++] = (float)stage->end_level * vertical_scale;
	}

	vgAppendPathData(renderer->state.path, command_index, commands, coords);

	// Render it out
	vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
	vgLoadIdentity();
	vgSetfv(VG_CLEAR_COLOR, 4, renderer->definition.background_colour);
	vgClear(renderer->definition.x, renderer->definition.y,
			renderer->definition.width, renderer->definition.height);

	vgSetf(VG_STROKE_LINE_WIDTH, renderer->definition.line_width);
	vgSeti(VG_STROKE_CAP_STYLE, renderer->definition.line_cap_style);
	vgSeti(VG_STROKE_JOIN_STYLE, renderer->definition.line_join_style);
	vgSetPaint(renderer->state.line_paint, VG_STROKE_PATH);
	vgSeti(VG_RENDERING_QUALITY, renderer->definition.rendering_quality);
	vgTranslate(renderer->definition.x, renderer->definition.y);
	vgDrawPath(renderer->state.path, VG_STROKE_PATH);
	vgClearPath(renderer->state.path, VG_PATH_CAPABILITY_APPEND_TO);

	vgSeti(VG_RENDERING_QUALITY, renderer->definition.text_quality);
	vgSetPaint(renderer->state.text_paint, VG_FILL_PATH);
	gfx_render_text(0, renderer->definition.height - 12, renderer->definition.text, &gfx_font_sans, 9);
}

//--------------------------------------------------------------------------------------------------------------
// Event handlers
//

static void refresh_handler(gfx_event_t *event, gfx_object_t *receiver)
{
	envelope_render_internal_t* renderer = (envelope_render_internal_t*)receiver;
	renderer->state.display_update = 1;
}

static void swap_handler(gfx_event_t *event, gfx_object_t *receiver)
{
	envelope_render_internal_t* renderer = (envelope_render_internal_t*)receiver;
	if (renderer->state.display_update)
	{
		update_display(renderer);
		renderer->state.display_update = 0;
	}
}

static void post_init_handler(gfx_event_t *event, gfx_object_t *receiver)
{
	initialise_renderer((envelope_render_internal_t*)receiver);
}

//--------------------------------------------------------------------------------------------------------------
// Public interface
//

void gfx_envelope_render_initialise()
{

}

void gfx_envelope_render_deinitialise()
{

}

envelope_renderer_t *gfx_envelope_renderer_create(object_id_t id)
{
	envelope_render_internal_t *renderer = (envelope_render_internal_t*) calloc(1, sizeof(envelope_render_internal_t));

	gfx_object_t *gfx_object = &renderer->definition.gfx_object;
	gfx_object->id = id;
	gfx_register_event_receiver_handler(GFX_EVENT_POSTINIT, post_init_handler, gfx_object);
	gfx_register_event_receiver_handler(GFX_EVENT_REFRESH, refresh_handler, gfx_object);
	gfx_register_event_receiver_handler(GFX_EVENT_BUFFERSWAP, swap_handler, gfx_object);

	renderer->definition.line_width 		= 1.0f;
	renderer->definition.line_cap_style 	= VG_CAP_BUTT;
	renderer->definition.line_join_style	= VG_JOIN_MITER;
	renderer->definition.rendering_quality	= VG_RENDERING_QUALITY_NONANTIALIASED;
	renderer->definition.text_quality		= VG_RENDERING_QUALITY_FASTER;


	return (envelope_renderer_t*)renderer;
}

void gfx_envelope_renderer_destroy(envelope_renderer_t *renderer)
{
	deinitialise_renderer((envelope_render_internal_t*)renderer);
	free(renderer);
}
