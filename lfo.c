/*
 * lfo.c
 *
 *  Created on: 8 Apr 2013
 *      Author: ntuckett
 */

#include "lfo.h"
#include <math.h>
#include "fixed_point_math.h"

mod_matrix_value_t lfo_generate_value(mod_matrix_source_t* source)
{
	lfo_t* lfo = (lfo_t*)source;
	return (mod_matrix_value_t) lfo->value;
}

void lfo_init(lfo_t *lfo, const char* name)
{
	mod_matrix_init_source(name, lfo_generate_value, &lfo->mod_matrix_source);
	mod_matrix_add_source(&lfo->mod_matrix_source);
	osc_init(&lfo->oscillator);
	lfo->oscillator.waveform = LFO_PROCEDURAL_SINE;
	lfo->oscillator.frequency = 1 * FIXED_ONE;
	lfo->oscillator.level = SHRT_MAX;
	lfo->value = 0;
}

void lfo_reset(lfo_t *lfo)
{
	lfo->oscillator.phase_accumulator = 0;
}

void lfo_update(lfo_t *lfo, int buffer_samples)
{
	osc_mid_output(&lfo->oscillator, &lfo->value, buffer_samples);
}
