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
 * waveform_procedural.c
 *
 *  Created on: 1 Nov 2012
 *      Author: ntuckett
 */

#include <stdlib.h>
#include <limits.h>
#include "waveform_procedural.h"
#include "system_constants.h"
#include "fixed_point_math.h"
#include "oscillator.h"

#define PHASE_LIMIT			(4 * FIXED_ONE)
#define PHASE_HALF_LIMIT	(2 * FIXED_ONE)
#define PHASE_QUARTER_LIMIT	(FIXED_ONE)

#define PR_CALC_PHASE_STEP(osc, phase_step)	fixed_t	phase_step = fixed_mul(PHASE_LIMIT, osc->frequency) / SYSTEM_SAMPLE_RATE

// Formulae (input is phase, t):
// 	0  to 2: 1 - (1-t)^2
// 	2> to 4: (1-(t-2))^2 - 1
#define PR_CALC_SINE_POSITIVE(osc, sample)	sample = FIXED_ONE - osc->phase_accumulator; \
											sample = fixed_mul(sample, sample); \
											sample = fixed_mul(FIXED_ONE - sample, SAMPLE_MAX)

#define PR_CALC_SINE_NEGATIVE(osc, sample)	sample = FIXED_ONE - (osc->phase_accumulator - PHASE_HALF_LIMIT); \
											sample = fixed_mul(sample, sample); \
											sample = fixed_mul(sample - FIXED_ONE, SAMPLE_MAX)

#define PR_CALC_HALFSINE(osc, sample)		sample = FIXED_ONE - (osc->phase_accumulator >> 1); \
											sample = fixed_mul(sample, sample); \
											sample = fixed_mul(FIXED_ONE - sample, SAMPLE_MAX)

#define PR_CALC_SAW_DOWN(osc, sample)		sample = FIXED_ONE - (osc->phase_accumulator >> 1);	\
											sample = fixed_mul(sample, SAMPLE_MAX);

#define PR_CALC_SAW_UP(osc, sample)			sample = osc->phase_accumulator >> 1;	\
											sample = fixed_mul(sample, SAMPLE_MAX);

#define PR_CALC_HALFSAW_DOWN(osc, sample)	sample = FIXED_ONE - (osc->phase_accumulator >> 2);	\
											sample = fixed_mul(sample, SAMPLE_MAX);

#define PR_CALC_HALFSAW_UP(osc, sample)		sample = osc->phase_accumulator >> 2;	\
											sample = fixed_mul(sample, SAMPLE_MAX);

#define PR_CALC_TRI_DOWN(osc, offs, sample)	sample = FIXED_ONE - (osc->phase_accumulator - offs);	\
											sample = fixed_mul(sample, SAMPLE_MAX);

#define PR_CALC_TRI_UP(osc, offs, sample)	sample = (osc->phase_accumulator - offs);	\
											sample = fixed_mul(sample, SAMPLE_MAX);

#define PR_CALC_HALFTRI_DOWN(osc, sample)	sample = FIXED_ONE - ((osc->phase_accumulator - PHASE_HALF_LIMIT) >> 1);	\
											sample = fixed_mul(sample, SAMPLE_MAX);

#define PR_CALC_HALFTRI_UP(osc, sample)		sample = (osc->phase_accumulator >> 1);	\
											sample = fixed_mul(sample, SAMPLE_MAX);

#define PR_ADVANCE_PHASE(osc, phase_step)	osc->phase_accumulator += phase_step

#define PR_LOOP_PHASE(osc)					if (osc->phase_accumulator >= PHASE_LIMIT) \
												osc->phase_accumulator -= PHASE_LIMIT

static void procedural_sine_output(waveform_generator_def_t *generator, oscillator_t* osc, sample_t *sample_data, int sample_count)
{
	PR_CALC_PHASE_STEP(osc, phase_step);
	sample_t *sample_ptr = sample_data;
	CALC_AMPLITUDE_INTERPOLATION(osc, amp_scale, amp_delta, sample_count);

	while (sample_count > 0)
	{
		while (osc->phase_accumulator < PHASE_HALF_LIMIT && sample_count > 0)
		{
			int32_t PR_CALC_SINE_POSITIVE(osc, sample);
			SCALE_AMPLITUDE((amp_scale >> AMPL_INTERP_PRECISION), sample);
			STORE_SAMPLE(sample, sample_ptr);
			PR_ADVANCE_PHASE(osc, phase_step);
			INTERPOLATE_AMPLITUDE(amp_scale, amp_delta);
			sample_count--;
		}

		while (osc->phase_accumulator < PHASE_LIMIT && sample_count > 0)
		{
			int32_t PR_CALC_SINE_NEGATIVE(osc, sample);
			SCALE_AMPLITUDE((amp_scale >> AMPL_INTERP_PRECISION), sample);
			STORE_SAMPLE(sample, sample_ptr);
			PR_ADVANCE_PHASE(osc, phase_step);
			INTERPOLATE_AMPLITUDE(amp_scale, amp_delta);
			sample_count--;
		}

		PR_LOOP_PHASE(osc);
	}
}

static void procedural_sine_mid_output(waveform_generator_def_t * generator, oscillator_t* osc, sample_t *sample_data, int sample_count)
{
	PR_CALC_PHASE_STEP(osc, phase_step);
	sample_t *sample_ptr = sample_data;

	phase_step *= sample_count / 2;	// step to midpoint
	PR_ADVANCE_PHASE(osc, phase_step);
	PR_LOOP_PHASE(osc);

	int32_t sample;

	if (osc->phase_accumulator < PHASE_HALF_LIMIT)
	{
		PR_CALC_SINE_POSITIVE(osc, sample);
	}
	else
	{
		PR_CALC_SINE_NEGATIVE(osc, sample);
	}

	SCALE_AMPLITUDE(osc->level, sample)
	*sample_ptr = sample;

	PR_ADVANCE_PHASE(osc, phase_step);
	PR_LOOP_PHASE(osc);
}

static void procedural_halfsine_mid_output(waveform_generator_def_t * generator, oscillator_t* osc, sample_t *sample_data, int sample_count)
{
	PR_CALC_PHASE_STEP(osc, phase_step);
	sample_t *sample_ptr = sample_data;

	phase_step *= sample_count / 2;	// step to midpoint
	PR_ADVANCE_PHASE(osc, phase_step);
	PR_LOOP_PHASE(osc);

	int32_t sample;

	PR_CALC_HALFSINE(osc, sample);

	SCALE_AMPLITUDE(osc->level, sample)
	*sample_ptr = sample;

	PR_ADVANCE_PHASE(osc, phase_step);
	PR_LOOP_PHASE(osc);
}

static void procedural_sine_mix_output(waveform_generator_def_t *generator, oscillator_t* osc, sample_t *sample_data, int sample_count)
{
	PR_CALC_PHASE_STEP(osc, phase_step);
	sample_t *sample_ptr = sample_data;
	CALC_AMPLITUDE_INTERPOLATION(osc, amp_scale, amp_delta, sample_count);

	while (sample_count > 0)
	{
		while (osc->phase_accumulator < PHASE_HALF_LIMIT && sample_count > 0)
		{
			int32_t PR_CALC_SINE_POSITIVE(osc, sample);
			SCALE_AMPLITUDE((amp_scale >> AMPL_INTERP_PRECISION), sample);
			MIX((int32_t)*sample_ptr, sample, mixed);
			STORE_SAMPLE(mixed, sample_ptr);
			PR_ADVANCE_PHASE(osc, phase_step);
			INTERPOLATE_AMPLITUDE(amp_scale, amp_delta);
			sample_count--;
		}

		while (osc->phase_accumulator < PHASE_LIMIT && sample_count > 0)
		{
			int32_t PR_CALC_SINE_NEGATIVE(osc, sample);
			SCALE_AMPLITUDE((amp_scale >> AMPL_INTERP_PRECISION), sample);
			MIX((int32_t)*sample_ptr, sample, mixed);
			STORE_SAMPLE(mixed, sample_ptr);
			PR_ADVANCE_PHASE(osc, phase_step);
			INTERPOLATE_AMPLITUDE(amp_scale, amp_delta);
			sample_count--;
		}

		PR_LOOP_PHASE(osc);
	}
}

static void procedural_saw_output(waveform_generator_def_t *generator, oscillator_t* osc, sample_t *sample_data, int sample_count)
{
	PR_CALC_PHASE_STEP(osc, phase_step);
	sample_t *sample_ptr = sample_data;
	CALC_AMPLITUDE_INTERPOLATION(osc, amp_scale, amp_delta, sample_count);

	while (sample_count > 0)
	{
		int32_t PR_CALC_SAW_DOWN(osc, sample);
		SCALE_AMPLITUDE((amp_scale >> AMPL_INTERP_PRECISION), sample);
		STORE_SAMPLE(sample, sample_ptr);
		PR_ADVANCE_PHASE(osc, phase_step);
		INTERPOLATE_AMPLITUDE(amp_scale, amp_delta);
		sample_count--;

		PR_LOOP_PHASE(osc);
	}
}

static void procedural_saw_mid_output(waveform_generator_def_t * generator, oscillator_t* osc, sample_t *sample_data, int sample_count)
{
	PR_CALC_PHASE_STEP(osc, phase_step);
	sample_t *sample_ptr = sample_data;

	phase_step *= sample_count / 2;	// step to midpoint
	PR_ADVANCE_PHASE(osc, phase_step);
	PR_LOOP_PHASE(osc);

	int32_t PR_CALC_SAW_DOWN(osc, sample);

	SCALE_AMPLITUDE(osc->level, sample)
	*sample_ptr = sample;

	PR_ADVANCE_PHASE(osc, phase_step);
	PR_LOOP_PHASE(osc);
}

static void procedural_sawup_mid_output(waveform_generator_def_t * generator, oscillator_t* osc, sample_t *sample_data, int sample_count)
{
	PR_CALC_PHASE_STEP(osc, phase_step);
	sample_t *sample_ptr = sample_data;

	phase_step *= sample_count / 2;	// step to midpoint
	PR_ADVANCE_PHASE(osc, phase_step);
	PR_LOOP_PHASE(osc);

	int32_t PR_CALC_SAW_UP(osc, sample);

	SCALE_AMPLITUDE(osc->level, sample)
	*sample_ptr = sample;

	PR_ADVANCE_PHASE(osc, phase_step);
	PR_LOOP_PHASE(osc);
}

static void procedural_saw_mix_output(waveform_generator_def_t *generator, oscillator_t* osc, sample_t *sample_data, int sample_count)
{
	PR_CALC_PHASE_STEP(osc, phase_step);
	sample_t *sample_ptr = sample_data;
	CALC_AMPLITUDE_INTERPOLATION(osc, amp_scale, amp_delta, sample_count);

	while (sample_count > 0)
	{
		int32_t PR_CALC_SAW_DOWN(osc, sample);
		SCALE_AMPLITUDE((amp_scale >> AMPL_INTERP_PRECISION), sample);
		MIX((int32_t)*sample_ptr, sample, mixed);
		STORE_SAMPLE(mixed, sample_ptr);
		PR_ADVANCE_PHASE(osc, phase_step);
		INTERPOLATE_AMPLITUDE(amp_scale, amp_delta);
		sample_count--;

		PR_LOOP_PHASE(osc);
	}
}

static void procedural_triangle_mid_output(waveform_generator_def_t * generator, oscillator_t* osc, sample_t *sample_data, int sample_count)
{
	PR_CALC_PHASE_STEP(osc, phase_step);
	sample_t *sample_ptr = sample_data;

	phase_step *= sample_count / 2;	// step to midpoint
	PR_ADVANCE_PHASE(osc, phase_step);
	PR_LOOP_PHASE(osc);

	int32_t sample;

	if (osc->phase_accumulator < PHASE_QUARTER_LIMIT)
	{
		PR_CALC_TRI_UP(osc, 0, sample);
	}
	else if (osc->phase_accumulator < PHASE_HALF_LIMIT)
	{
		PR_CALC_TRI_DOWN(osc, PHASE_QUARTER_LIMIT, sample);
	}
	else if (osc->phase_accumulator < PHASE_HALF_LIMIT + PHASE_QUARTER_LIMIT)
	{
		PR_CALC_TRI_UP(osc, PHASE_HALF_LIMIT, sample);
		sample = -sample;
	}
	else
	{
		PR_CALC_TRI_DOWN(osc, (PHASE_HALF_LIMIT + PHASE_QUARTER_LIMIT), sample);
		sample = -sample;
	}

	SCALE_AMPLITUDE(osc->level, sample)
	*sample_ptr = sample;

	PR_ADVANCE_PHASE(osc, phase_step);
	PR_LOOP_PHASE(osc);
}

static void procedural_square_mid_output(waveform_generator_def_t * generator, oscillator_t* osc, sample_t *sample_data, int sample_count)
{
	PR_CALC_PHASE_STEP(osc, phase_step);
	sample_t *sample_ptr = sample_data;

	phase_step *= sample_count / 2;	// step to midpoint
	PR_ADVANCE_PHASE(osc, phase_step);
	PR_LOOP_PHASE(osc);

	int32_t sample = (((osc->phase_accumulator - PHASE_HALF_LIMIT) >> (sizeof(osc->phase_accumulator) * 8 - 1)) | 1) * SAMPLE_MAX;

	SCALE_AMPLITUDE(osc->level, sample)
	*sample_ptr = sample;

	PR_ADVANCE_PHASE(osc, phase_step);
	PR_LOOP_PHASE(osc);
}


static void procedural_halfsawdown_mid_output(waveform_generator_def_t * generator, oscillator_t* osc, sample_t *sample_data, int sample_count)
{
	PR_CALC_PHASE_STEP(osc, phase_step);
	sample_t *sample_ptr = sample_data;

	phase_step *= sample_count / 2;	// step to midpoint
	PR_ADVANCE_PHASE(osc, phase_step);
	PR_LOOP_PHASE(osc);

	int32_t PR_CALC_HALFSAW_DOWN(osc, sample);

	SCALE_AMPLITUDE(osc->level, sample)
	*sample_ptr = sample;

	PR_ADVANCE_PHASE(osc, phase_step);
	PR_LOOP_PHASE(osc);
}

static void procedural_halfsawup_mid_output(waveform_generator_def_t * generator, oscillator_t* osc, sample_t *sample_data, int sample_count)
{
	PR_CALC_PHASE_STEP(osc, phase_step);
	sample_t *sample_ptr = sample_data;

	phase_step *= sample_count / 2;	// step to midpoint
	PR_ADVANCE_PHASE(osc, phase_step);
	PR_LOOP_PHASE(osc);

	int32_t PR_CALC_HALFSAW_UP(osc, sample);

	SCALE_AMPLITUDE(osc->level, sample)
	*sample_ptr = sample;

	PR_ADVANCE_PHASE(osc, phase_step);
	PR_LOOP_PHASE(osc);
}

static void procedural_halftriangle_mid_output(waveform_generator_def_t * generator, oscillator_t* osc, sample_t *sample_data, int sample_count)
{
	PR_CALC_PHASE_STEP(osc, phase_step);
	sample_t *sample_ptr = sample_data;

	phase_step *= sample_count / 2;	// step to midpoint
	PR_ADVANCE_PHASE(osc, phase_step);
	PR_LOOP_PHASE(osc);

	int32_t sample;

	if (osc->phase_accumulator < PHASE_HALF_LIMIT)
	{
		PR_CALC_HALFTRI_UP(osc, sample);
	}
	else
	{
		PR_CALC_HALFTRI_DOWN(osc, sample);
	}

	SCALE_AMPLITUDE(osc->level, sample)
	*sample_ptr = sample;

	PR_ADVANCE_PHASE(osc, phase_step);
	PR_LOOP_PHASE(osc);
}

void init_procedural_generator(waveform_type_t waveform_type, waveform_generator_t *generator)
{
	switch (waveform_type)
	{
		case PROCEDURAL_SINE:
			generator->output_func = procedural_sine_output;
			generator->mix_func = procedural_sine_mix_output;
			generator->mid_func = NULL;
			break;

		case PROCEDURAL_SAW:
			generator->output_func = procedural_saw_output;
			generator->mix_func = procedural_saw_mix_output;
			generator->mid_func = NULL;
			break;

		case LFO_PROCEDURAL_SINE:
			generator->output_func = NULL;
			generator->mix_func = NULL;
			generator->mid_func = procedural_sine_mid_output;
			break;

		case LFO_PROCEDURAL_SAW_DOWN:
			generator->output_func = NULL;
			generator->mix_func = NULL;
			generator->mid_func = procedural_saw_mid_output;
			break;

		case LFO_PROCEDURAL_SAW_UP:
			generator->output_func = NULL;
			generator->mix_func = NULL;
			generator->mid_func = procedural_sawup_mid_output;
			break;

		case LFO_PROCEDURAL_TRIANGLE:
			generator->output_func = NULL;
			generator->mix_func = NULL;
			generator->mid_func = procedural_triangle_mid_output;
			break;

		case LFO_PROCEDURAL_SQUARE:
			generator->output_func = NULL;
			generator->mix_func = NULL;
			generator->mid_func = procedural_square_mid_output;
			break;

		case LFO_PROCEDURAL_HALFSAW_DOWN:
			generator->output_func = NULL;
			generator->mix_func = NULL;
			generator->mid_func = procedural_halfsawdown_mid_output;
			break;

		case LFO_PROCEDURAL_HALFSAW_UP:
			generator->output_func = NULL;
			generator->mix_func = NULL;
			generator->mid_func = procedural_halfsawup_mid_output;
			break;

		case LFO_PROCEDURAL_HALFSINE:
			generator->output_func = NULL;
			generator->mix_func = NULL;
			generator->mid_func = procedural_halfsine_mid_output;
			break;

		case LFO_PROCEDURAL_HALFTRIANGLE:
			generator->output_func = NULL;
			generator->mix_func = NULL;
			generator->mid_func = procedural_halftriangle_mid_output;
			break;

		default:
			break;
	}

	generator->definition.flags = GENFLAG_NONE;
	generator->definition.waveform_data = NULL;
}
