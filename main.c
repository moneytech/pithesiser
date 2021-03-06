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
 * main.c
 *
 *  Created on: 29 Oct 2012
 *      Author: ntuckett
 */

#include <unistd.h>
#include <math.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <libgen.h>
#include <libconfig.h>
#include <gperftools/profiler.h>

#include "system_constants.h"
#include "master_time.h"
#include "logging.h"
#include "alsa.h"
#include "midi.h"

#include "gfx.h"
#include "gfx_event.h"
#include "gfx_event_types.h"
#include "gfx_wave_render.h"
#include "gfx_envelope_render.h"
#include "gfx_setting_render.h"
#include "gfx_image.h"
#include "synth_model.h"
#include "synth_controllers.h"
#include "modulation_matrix_controller.h"

#include "setting.h"
#include "recording.h"
#include "code_timing_tests.h"
#include "piglow.h"

//-----------------------------------------------------------------------------------------------------------------------
// Commons
//
#define VOICE_COUNT				8
#define EXIT_CONTROLLER			0x2e
#define PROFILE_CONTROLLER		0x2c

static const char* RESOURCES_PITHESISER_ALPHA_PNG = "resources/pithesiser_alpha.png";
static const char* RESOURCES_SYNTH_CFG = "resources/synth.cfg";

static const char* CFG_DEVICES_AUDIO_OUTPUT = "devices.audio.output";
static const char* CFG_DEVICES_AUDIO_AUTO_DUCK = "devices.audio.auto_duck";
static const char* CFG_DEVICES_MIDI_NOTE_CHANNEL = "devices.midi.note_channel";
static const char* CFG_DEVICES_MIDI_CONTROLLER_CHANNEL = "devices.midi.controller_channel";
static const char* CFG_DEVICES_PIGLOW = "devices.piglow";
static const char* CFG_CONTROLLERS = "controllers";
static const char* CFG_MOD_MATRIX_CONTROLLER = "modulation_matrix";
static const char* CFG_DEVICES_MIDI_INPUT = "devices.midi.input";
static const char* CFG_TESTS = "tests";
static const char* CFG_CODE_TIMING_TESTS = "code_timing";
static const char* CFG_SYSEX_INIT = "sysex.init_message";

static const char* SETTINGS_FILE = ".pithesiser.cfg";
static const char* PATCH_FILE = ".pithesiser.patch";

config_t app_config;
config_t patch_config;

//-----------------------------------------------------------------------------------------------------------------------
// Synth model
//
synth_model_t synth_model;

//-----------------------------------------------------------------------------------------------------------------------
// Settings
//
const char* master_waveform_names[] =
{
	"WAVETABLE_SINE",
	"WAVETABLE_SAW",
	"WAVETABLE_SAW_BL",
	"WAVETABLE_SINE_LINEAR",
	"WAVETABLE_SAW_LINEAR",
	"WAVETABLE_SAW_LINEAR_BL",
	"PROCEDURAL_SINE",
	"PROCEDURAL_SAW",
};

enum_type_info_t master_waveform_type =
{
	WAVE_LAST_AUDIBLE,
	master_waveform_names
};

void create_settings()
{
	setting_create("master-volume");
	setting_create("master-waveform");

	synth_model.setting_master_volume = setting_find("master-volume");
	synth_model.setting_master_waveform = setting_find("master-waveform");
	setting_init_as_int(synth_model.setting_master_volume, 0);
	setting_init_as_enum(synth_model.setting_master_waveform, (int)WAVETABLE_SINE, &master_waveform_type);
}

void destroy_settings()
{
	setting_destroy(synth_model.setting_master_volume);
	setting_destroy(synth_model.setting_master_waveform);
	synth_model.setting_master_volume = NULL;
	synth_model.setting_master_waveform = NULL;
}

//-----------------------------------------------------------------------------------------------------------------------
// Audio processing
//
static int32_t duck_level_by_voice_count[] = { LEVEL_MAX, LEVEL_MAX, LEVEL_MAX * 0.65f, LEVEL_MAX * 0.49f, LEVEL_MAX * 0.40f,
											   LEVEL_MAX * 0.34f, LEVEL_MAX * 0.29f, LEVEL_MAX * 0.25f, LEVEL_MAX * 0.22f};

void configure_audio()
{
	const char *setting_devices_audio_output = NULL;

	if (config_lookup_string(&app_config, CFG_DEVICES_AUDIO_OUTPUT, &setting_devices_audio_output) != CONFIG_TRUE)
	{
		LOG_ERROR("Missing audio output device in config");
		exit(EXIT_FAILURE);
	}

	if (alsa_initialise(setting_devices_audio_output, 128) < 0)
	{
		exit(EXIT_FAILURE);
	}

	config_setting_t *setting_auto_duck = config_lookup(&app_config, CFG_DEVICES_AUDIO_AUTO_DUCK);

	if (setting_auto_duck != NULL)
	{
		int duck_setting_count = config_setting_length(setting_auto_duck);

		if (duck_setting_count != synth_model.voice_count)
		{
			LOG_ERROR("Invalid number of auto duck levels %d - should be %d", duck_setting_count, synth_model.voice_count);
			exit(EXIT_FAILURE);
		}

		for (int i = 0; i < duck_setting_count; i++)
		{
			float duck_level_factor = config_setting_get_float_elem(setting_auto_duck, i);
			if (duck_level_factor < 0.0f || duck_level_factor > 1.0f)
			{
				LOG_ERROR("Invalid auto duck level %d of %f - should be between 0 and 1", i + 1, duck_level_factor);
				exit(EXIT_FAILURE);
			}

			int32_t duck_level = LEVEL_MAX * duck_level_factor;

			if (duck_level > duck_level_by_voice_count[i])
			{
				LOG_ERROR("Invalid auto duck level %d of %f - values should be decreasing", i + 1, duck_level_factor);
				exit(EXIT_FAILURE);
			}

			duck_level_by_voice_count[i + 1] = duck_level;
		}
	}

	synth_model_set_ducking_levels(&synth_model, duck_level_by_voice_count);
}

void process_audio(int32_t timestep_ms)
{
	int write_buffer_index = alsa_lock_next_write_buffer();
	void* buffer_data;
	int buffer_samples;
	alsa_get_buffer_params(write_buffer_index, &buffer_data, &buffer_samples);
	size_t buffer_bytes = buffer_samples * sizeof(sample_t) * 2;

	synth_update_state_t update_state;
	update_state.timestep_ms = timestep_ms;
	update_state.sample_count = buffer_samples;
	update_state.buffer_data = buffer_data;
	synth_model_update(&synth_model, &update_state);

	gfx_event_t gfx_event;
	gfx_event.type = GFX_EVENT_WAVE;
	gfx_event.flags = GFX_EVENT_FLAG_OWNPTR;
	gfx_event.ptr = malloc(buffer_bytes);
	gfx_event.receiver_id = WAVE_RENDERER_ID;
	if (gfx_event.ptr != NULL)
	{
		gfx_event.size = buffer_bytes;
		memcpy(gfx_event.ptr, buffer_data, buffer_bytes);
		gfx_send_event(&gfx_event);
	}

	alsa_unlock_buffer(write_buffer_index);
}

//-----------------------------------------------------------------------------------------------------------------------
// Midi processing
//
int controller_channel = 0;

void configure_midi()
{
	int note_channel = 0;

	config_lookup_int(&app_config, CFG_DEVICES_MIDI_CONTROLLER_CHANNEL, &controller_channel);
	config_lookup_int(&app_config, CFG_DEVICES_MIDI_NOTE_CHANNEL, &note_channel);

	synth_model_set_midi_channel(&synth_model, note_channel);

	config_setting_t *setting_devices_midi_input = config_lookup(&app_config, CFG_DEVICES_MIDI_INPUT);

	if (setting_devices_midi_input == NULL)
	{
		LOG_ERROR("Missing midi input devices in config");
		exit(EXIT_FAILURE);
	}

	const char* midi_device_names[MAX_MIDI_DEVICES];
	int midi_device_count = config_setting_length(setting_devices_midi_input);

	if (midi_device_count < 0 || midi_device_count > MAX_MIDI_DEVICES)
	{
		LOG_ERROR("Invalid number of midi devices %d - should be between 1 and %d", midi_device_count, MAX_MIDI_DEVICES);
		exit(EXIT_FAILURE);
	}

	for (int i = 0; i < midi_device_count; i++)
	{
		midi_device_names[i] = config_setting_get_string_elem(setting_devices_midi_input, i);
	}

	if (midi_initialise(midi_device_count, midi_device_names) < 0)
	{
		exit(EXIT_FAILURE);
	}

	if (!synth_controllers_initialise(controller_channel, config_lookup(&app_config, CFG_CONTROLLERS)))
	{
		exit(EXIT_FAILURE);
	}

	if (mod_matrix_controller_initialise(config_lookup(&app_config, CFG_MOD_MATRIX_CONTROLLER), config_lookup(&patch_config, CFG_MOD_MATRIX_CONTROLLER)) != RESULT_OK)
	{
		exit(EXIT_FAILURE);
	}

	synth_controllers_load(SETTINGS_FILE, &synth_model);

	config_setting_t *setting_sysex_init_message = config_lookup(&app_config, CFG_SYSEX_INIT);
	if (setting_sysex_init_message != NULL)
	{
		int message_len = config_setting_length(setting_sysex_init_message);
		char *message_bytes = alloca(message_len);
		for (int i = 0; i < message_len; i++)
		{
			message_bytes[i] = config_setting_get_int_elem(setting_sysex_init_message, i);
		}

		midi_send_sysex(message_bytes, message_len);
	}
}

void process_midi_events()
{
	int midi_events = midi_get_event_count();

	while (midi_events-- > 0)
	{
		midi_event_t midi_event;
		midi_pop_event(&midi_event);
		unsigned char event_type = midi_event.type & 0xf0;
		int channel = midi_event.type & 0x0f;

		mod_matrix_controller_process_midi(midi_event.device_handle, channel, event_type, midi_event.data[0], midi_event.data[1]);

		if (event_type == 0x90)
		{
			synth_model_play_note(&synth_model, channel, midi_event.data[0]);
		}
		else if (event_type == 0x80)
		{
			synth_model_stop_note(&synth_model, channel, midi_event.data[0]);
		}
	}
}

//-----------------------------------------------------------------------------------------------------------------------
// UI data & management
//
wave_renderer_t *waveform_renderer = NULL;
envelope_renderer_t *envelope_renderer = NULL;
image_renderer_t *image_renderer = NULL;
envelope_renderer_t *freq_envelope_renderer = NULL;
envelope_renderer_t *q_envelope_renderer = NULL;
setting_renderer_t *master_volume_renderer = NULL;
setting_renderer_t *master_waveform_renderer = NULL;

void process_postinit_ui(gfx_event_t *event, gfx_object_t *receiver)
{
	int screen_width;
	int screen_height;

	gfx_get_screen_resolution(&screen_width, &screen_height);

	image_renderer->y = screen_height - image_renderer->height;
}

void create_ui()
{
	gfx_register_event_global_handler(GFX_EVENT_POSTINIT, process_postinit_ui);

	waveform_renderer = gfx_wave_renderer_create(WAVE_RENDERER_ID);

	waveform_renderer->x = 0;
	waveform_renderer->y = 0;
	waveform_renderer->width = 1024;
	waveform_renderer->height = 512;
	waveform_renderer->amplitude_scale = 129;
	waveform_renderer->tuned_wavelength_fx = 129;
	waveform_renderer->background_colour[0] = 0.0f;
	waveform_renderer->background_colour[1] = 0.0f;
	waveform_renderer->background_colour[2] = 16.0f;
	waveform_renderer->background_colour[3] = 255.0f;
	waveform_renderer->line_colour[0] = 0.0f;
	waveform_renderer->line_colour[1] = 255.0f;
	waveform_renderer->line_colour[2] = 0.0f;
	waveform_renderer->line_colour[3] = 255.0f;

	envelope_renderer = gfx_envelope_renderer_create(ENVELOPE_1_RENDERER_ID);

	envelope_renderer->x = 0;
	envelope_renderer->y = 514;
	envelope_renderer->width = 510;
	envelope_renderer->height = 240;
	envelope_renderer->envelope = &synth_model.envelope[0];
	envelope_renderer->background_colour[0] = 0.0f;
	envelope_renderer->background_colour[1] = 0.0f;
	envelope_renderer->background_colour[2] = 16.0f;
	envelope_renderer->background_colour[3] = 255.0f;
	envelope_renderer->line_colour[0] = 0.0f;
	envelope_renderer->line_colour[1] = 255.0f;
	envelope_renderer->line_colour[2] = 0.0f;
	envelope_renderer->line_colour[3] = 255.0f;
	envelope_renderer->text = "envelope-1";
	envelope_renderer->text_colour[0] = 255.0f;
	envelope_renderer->text_colour[1] = 255.0f;
	envelope_renderer->text_colour[2] = 255.0f;
	envelope_renderer->text_colour[3] = 255.0f;

	freq_envelope_renderer = gfx_envelope_renderer_create(ENVELOPE_2_RENDERER_ID);

	freq_envelope_renderer->x = 512;
	freq_envelope_renderer->y = 514;
	freq_envelope_renderer->width = 512;
	freq_envelope_renderer->height = 119;
	freq_envelope_renderer->envelope = &synth_model.envelope[1];
	freq_envelope_renderer->background_colour[0] = 0.0f;
	freq_envelope_renderer->background_colour[1] = 0.0f;
	freq_envelope_renderer->background_colour[2] = 16.0f;
	freq_envelope_renderer->background_colour[3] = 255.0f;
	freq_envelope_renderer->line_colour[0] = 0.0f;
	freq_envelope_renderer->line_colour[1] = 255.0f;
	freq_envelope_renderer->line_colour[2] = 0.0f;
	freq_envelope_renderer->line_colour[3] = 255.0f;
	freq_envelope_renderer->text = "envelope-2";
	freq_envelope_renderer->text_colour[0] = 255.0f;
	freq_envelope_renderer->text_colour[1] = 255.0f;
	freq_envelope_renderer->text_colour[2] = 255.0f;
	freq_envelope_renderer->text_colour[3] = 255.0f;

	q_envelope_renderer = gfx_envelope_renderer_create(ENVELOPE_3_RENDERER_ID);

	q_envelope_renderer->x = 512;
	q_envelope_renderer->y = 634;
	q_envelope_renderer->width = 512;
	q_envelope_renderer->height = 120;
	q_envelope_renderer->envelope = &synth_model.envelope[2];
	q_envelope_renderer->background_colour[0] = 0.0f;
	q_envelope_renderer->background_colour[1] = 0.0f;
	q_envelope_renderer->background_colour[2] = 16.0f;
	q_envelope_renderer->background_colour[3] = 255.0f;
	q_envelope_renderer->line_colour[0] = 0.0f;
	q_envelope_renderer->line_colour[1] = 255.0f;
	q_envelope_renderer->line_colour[2] = 0.0f;
	q_envelope_renderer->line_colour[3] = 255.0f;
	q_envelope_renderer->text = "envelope-3";
	q_envelope_renderer->text_colour[0] = 255.0f;
	q_envelope_renderer->text_colour[1] = 255.0f;
	q_envelope_renderer->text_colour[2] = 255.0f;
	q_envelope_renderer->text_colour[3] = 255.0f;

	image_renderer = gfx_image_renderer_create(IMAGE_RENDERER_ID);
	image_renderer->x = 0;
	image_renderer->y = 0;
	image_renderer->width = 157;
	image_renderer->height = 140;
	image_renderer->image_file = (char*)RESOURCES_PITHESISER_ALPHA_PNG;

	master_volume_renderer = gfx_setting_renderer_create(MASTER_VOLUME_RENDERER_ID);
	master_volume_renderer->x		= 1026;
	master_volume_renderer->y		= 0;
	master_volume_renderer->width	= 300;
	master_volume_renderer->height	= 14;
	master_volume_renderer->background_colour[0] = 0.0f;
	master_volume_renderer->background_colour[1] = 0.0f;
	master_volume_renderer->background_colour[2] = 16.0f;
	master_volume_renderer->background_colour[3] = 255.0f;
	master_volume_renderer->text = "master vol:";
	master_volume_renderer->text_colour[0] = 255.0f;
	master_volume_renderer->text_colour[1] = 255.0f;
	master_volume_renderer->text_colour[2] = 255.0f;
	master_volume_renderer->text_colour[3] = 255.0f;
	master_volume_renderer->text_size = 9;
	master_volume_renderer->text_x_offset = 1;
	master_volume_renderer->text_y_offset = 1;
	master_volume_renderer->setting = synth_model.setting_master_volume;
	master_volume_renderer->format = "%05d";

	master_waveform_renderer = gfx_setting_renderer_create(MASTER_WAVEFORM_RENDERER_ID);
	master_waveform_renderer->x		= 1026;
	master_waveform_renderer->y		= 14;
	master_waveform_renderer->width	= 300;
	master_waveform_renderer->height = 14;
	master_waveform_renderer->background_colour[0] = 0.0f;
	master_waveform_renderer->background_colour[1] = 0.0f;
	master_waveform_renderer->background_colour[2] = 16.0f;
	master_waveform_renderer->background_colour[3] = 255.0f;
	master_waveform_renderer->text = "master wave:";
	master_waveform_renderer->text_colour[0] = 255.0f;
	master_waveform_renderer->text_colour[1] = 255.0f;
	master_waveform_renderer->text_colour[2] = 255.0f;
	master_waveform_renderer->text_colour[3] = 255.0f;
	master_waveform_renderer->text_size = 9;
	master_waveform_renderer->text_x_offset = 1;
	master_waveform_renderer->text_y_offset = 3;
	master_waveform_renderer->setting = synth_model.setting_master_waveform;
	master_waveform_renderer->format = "%s";
}

void tune_oscilloscope_to_note(int note)
{
	int note_wavelength = midi_get_note_wavelength_samples(note);
	gfx_wave_render_wavelength(waveform_renderer, note_wavelength);
}

void destroy_ui()
{
	gfx_setting_renderer_destroy(master_waveform_renderer);
	gfx_setting_renderer_destroy(master_volume_renderer);
	gfx_image_renderer_destroy(image_renderer);
	gfx_envelope_renderer_destroy(q_envelope_renderer);
	gfx_envelope_renderer_destroy(freq_envelope_renderer);
	gfx_envelope_renderer_destroy(envelope_renderer);
	gfx_wave_renderer_destroy(waveform_renderer);
}

//-----------------------------------------------------------------------------------------------------------------------
// GFX event handlers
//

void process_buffer_swap(gfx_event_t *event, gfx_object_t *receiver)
{
	process_synth_controllers(&synth_model);
	piglow_update(synth_model.voice, synth_model.voice_count);

	int param_value;

	if (midi_controller_update_and_read(&oscilloscope_controller, &param_value))
	{
		tune_oscilloscope_to_note(param_value);
	}
}

//-----------------------------------------------------------------------------------------------------------------------
// Patch configuration
//

void patch_initialise()
{
	config_init(&patch_config);

	if (access(PATCH_FILE, F_OK) != -1)
	{
		if (config_read_file(&patch_config, PATCH_FILE) != CONFIG_TRUE)
		{
			LOG_ERROR("Patch load error in %s at line %d: %s", config_error_file(&patch_config), config_error_line(&patch_config), config_error_text(&patch_config));
			exit(EXIT_FAILURE);
		}
	}
}

void patch_save()
{
	config_setting_t* root_setting = config_root_setting(&patch_config);

	config_setting_remove(root_setting, CFG_MOD_MATRIX_CONTROLLER);
	config_setting_t* mod_matrix_patch = config_setting_add(root_setting, CFG_MOD_MATRIX_CONTROLLER, CONFIG_TYPE_GROUP);
	mod_matrix_controller_save(mod_matrix_patch);

	if (config_write_file(&patch_config, PATCH_FILE) != CONFIG_TRUE)
	{
		LOG_ERROR("Patch write error to %s", PATCH_FILE);
	}
}

void patch_deinitialise()
{
	config_destroy(&patch_config);
}

//-----------------------------------------------------------------------------------------------------------------------
// Synth model
//

void synth_initialise()
{
	mod_matrix_initialise();
	synth_model_initialise(&synth_model, VOICE_COUNT);
}

void synth_deinitialise()
{
	destroy_settings();
	synth_model_deinitialise(&synth_model);
}

//-----------------------------------------------------------------------------------------------------------------------
// Synth main loop
//
const char *profile_file = NULL;

void configure_profiling()
{
	config_lookup_string(&app_config, "profiling.output_file", &profile_file);
}

void synth_main()
{
	gfx_wave_render_initialise();
	gfx_envelope_render_initialise();

	create_settings();
	create_ui();

	gfx_event_initialise();
	gfx_initialise();

	patch_initialise();
	synth_initialise();
	configure_audio();
	configure_profiling();

	config_setting_t* piglow_config = config_lookup(&app_config, CFG_DEVICES_PIGLOW);
	if (piglow_config != NULL)
	{
		piglow_initialise(piglow_config);
	}

	waveform_initialise();
	gfx_register_event_global_handler(GFX_EVENT_BUFFERSWAP, process_buffer_swap);

	// Done after synth setup as this can load controller values into the synth
	configure_midi();

	int profiling = 0;

	int32_t last_timestamp = get_elapsed_time_ms();

	while (1)
	{
		int midi_controller_value;

		if (midi_controller_update_and_read(&exit_controller, &midi_controller_value))
		{
			break;
		}

		if (midi_controller_update_and_read(&screenshot_controller, &midi_controller_value))
		{
			static int screenshot_count = 0;
			char screenshot_name[64];
			sprintf(screenshot_name, "pithesiser-img-%03d.png", screenshot_count++);
			gfx_screenshot(screenshot_name);
		}

		process_midi_events();

		if (!profiling && midi_controller_update_and_read(&profile_controller, &midi_controller_value))
		{
			if (profile_file != NULL)
			{
				ProfilerStart(profile_file);
				profiling = 1;
			}
		}

		alsa_sync_with_audio_output();

		int32_t timestamp = get_elapsed_time_ms();
		process_audio(timestamp - last_timestamp);
		last_timestamp = timestamp;
	}

	if (profiling)
	{
		ProfilerStop();
	}

	synth_controllers_save(SETTINGS_FILE);
	patch_save();
	patch_deinitialise();
	piglow_deinitialise();
	gfx_deinitialise();
	destroy_ui();
	gfx_envelope_render_deinitialise();
	gfx_wave_render_deinitialise();
	mod_matrix_controller_deinitialise();
	alsa_deinitialise();
	midi_deinitialise();
	synth_deinitialise();
	config_destroy(&app_config);

	LOG_INFO("Done: %d xruns", alsa_get_xruns_count());
}

//-----------------------------------------------------------------------------------------------------------------------
// Entrypoint
//

int main(int argc, char **argv)
{
	if (logging_initialise() != RESULT_OK)
	{
		exit(EXIT_FAILURE);
	}

	const char* config_file = RESOURCES_SYNTH_CFG;
	if (argc > 1)
	{
		config_file = argv[1];
	}

	char config_dir[PATH_MAX];
	realpath(config_file, config_dir);
	dirname(config_dir);
	config_init(&app_config);
	config_set_include_dir(&app_config, config_dir);
	if (config_read_file(&app_config, config_file) != CONFIG_TRUE)
	{
		LOG_ERROR("Config error in %s at line %d: %s", config_error_file(&app_config), config_error_line(&app_config), config_error_text(&app_config));
		exit(EXIT_FAILURE);
	}

	config_setting_t *setting_tests = config_lookup(&app_config, CFG_TESTS);

	if (setting_tests != NULL)
	{
		config_setting_t *setting_code_timing_tests = config_setting_get_member(setting_tests, CFG_CODE_TIMING_TESTS);
		if (setting_code_timing_tests != NULL)
		{
			code_timing_tests_main(setting_code_timing_tests);
		}
	}
	else
	{
		recording_initialise(&app_config, WAVE_RENDERER_ID);
		synth_main();
		recording_deinitialise();
	}

	return 0;
}
