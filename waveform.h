/*
 * waveform.h
 *
 *  Created on: 1 Nov 2012
 *      Author: ntuckett
 */

#ifndef WAVEFORM_H_
#define WAVEFORM_H_

#include <sys/types.h>

typedef enum
{
	WAVETABLE_SINE = 0,
	WAVETABLE_SAW,
	WAVETABLE_SAW_BL,
	WAVETABLE_SINE_LINEAR,
	WAVETABLE_SAW_LINEAR,
	WAVETABLE_SAW_LINEAR_BL,

	PROCEDURAL_SINE,
	PROCEDURAL_SAW,

	LFO_PROCEDURAL_SINE,
	LFO_PROCEDURAL_SAW_DOWN,
	LFO_PROCEDURAL_SAW_UP,
	LFO_PROCEDURAL_TRIANGLE,
	LFO_PROCEDURAL_SQUARE,
	LFO_PROCEDURAL_HALFSAW_DOWN,
	LFO_PROCEDURAL_HALFSAW_UP,
	LFO_PROCEDURAL_HALFSINE,
	LFO_PROCEDURAL_HALFTRIANGLE,

	WAVE_FIRST_AUDIBLE =	WAVETABLE_SINE,
	WAVE_LAST_AUDIBLE = 	PROCEDURAL_SAW,
	WAVE_FIRST_LFO = 		LFO_PROCEDURAL_SINE,
	WAVE_LAST_LFO =			LFO_PROCEDURAL_HALFTRIANGLE,

	WAVE_COUNT
} waveform_type_t;

extern void waveform_initialise();

#endif /* WAVEFORM_H_ */
