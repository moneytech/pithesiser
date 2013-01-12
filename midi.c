/*
 * midi.c
 *
 *  Created on: 30 Oct 2012
 *      Author: ntuckett
 *
 *
 *  Captures continuous controller changes in its own thread
 *
 *  0xBy CNumber CValue
 *
 *  where y is channel number (0-15)
 *  CNumber is controller id
 *  CValue is value (0-127)
 *
 *  Channel  0: max number of controllers is 127
 *  Channels 1-15: max number of controllers per channel is 17
 */

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include "midi.h"
#include "waveform_internal.h"

#define CHANNEL_COUNT	16
#define MIDI_NOTE_COUNT	128

typedef struct
{
	int		channel;
	int		controller_count;
	char*	controller_data;
	char*	controller_flag;
} channel_data;

channel_data channels[CHANNEL_COUNT] =
{
	{  0, 127, NULL },
	{  1,  17, NULL },
	{  2,  17, NULL },
	{  3,  17, NULL },
	{  4,  17, NULL },
	{  5,  17, NULL },
	{  6,  17, NULL },
	{  7,  17, NULL },
	{  8,  17, NULL },
	{  9,  17, NULL },
	{ 10,  17, NULL },
	{ 11,  17, NULL },
	{ 12,  17, NULL },
	{ 13,  17, NULL },
	{ 14,  17, NULL },
	{ 15,  17, NULL },
};

int32_t note_frequency[MIDI_NOTE_COUNT] =
{
	2143237, 2270680, 2405702, 2548752, 2700309, 2860878, 3030994, 3211227, 3402176, 3604480, 3818814, 4045892, 4286473, 4541360, 4811404, 5097505, 5400618, 5721755, 6061989, 6422453, 6804352, 7208960, 7637627, 8091784, 8572947, 9082720, 9622807, 10195009, 10801236, 11443511, 12123977, 12844906, 13608704, 14417920, 15275254, 16183568, 17145893, 18165441, 19245614, 20390018, 21602472, 22887021, 24247954, 25689813, 27217409, 28835840, 30550508, 32367136, 34291786, 36330882, 38491228, 40780036, 43204943, 45774043, 48495909, 51379626, 54434817, 57671680, 61101017, 64734272, 68583572, 72661764, 76982457, 81560072, 86409886, 91548086, 96991818, 102759252, 108869635, 115343360, 122202033, 129468544, 137167144, 145323527, 153964914, 163120144, 172819773, 183096171, 193983636, 205518503, 217739269, 230686720, 244404066, 258937088, 274334289, 290647054, 307929828, 326240288, 345639545, 366192342, 387967272, 411037006, 435478539, 461373440, 488808132, 517874176, 548668578, 581294109, 615859655, 652480576, 691279090, 732384684, 775934544, 822074013, 870957077, 922746880, 977616265, 1035748353, 1097337155, 1162588218, 1231719311, 1304961152, 1382558180, 1464769368, 1551869087, 1644148025, 1741914154, 1845493760, 1955232530, 2071496706, 2194674310, 2325176436, 2463438621, 2609922305, 2765116361, 2929538736, 3103738174, 3288296050
};

int 		midi_handle1 = -1;
int 		midi_handle2 = -1;
pthread_t	midi_thread_handle;

#define MIDI_EVENT_BUFFER_SIZE	128
#define MIDI_EVENT_BUFFER_MASK	(MIDI_EVENT_BUFFER_SIZE - 1)

pthread_mutex_t	midi_buffer_lock = PTHREAD_MUTEX_INITIALIZER;

midi_event_t midi_event_buffer[MIDI_EVENT_BUFFER_SIZE];
int midi_event_buffer_write_index = 0;
int midi_event_buffer_read_index = 0;

void midi_push_event(unsigned char type, size_t data_length, unsigned char *data)
{
	pthread_mutex_lock(&midi_buffer_lock);

	int next_write_index = (midi_event_buffer_write_index + 1) & MIDI_EVENT_BUFFER_MASK;
	if (next_write_index != midi_event_buffer_read_index)
	{
		size_t copy_size;

		if (data_length < MIDI_EVENT_DATA_SIZE)
		{
			copy_size = data_length;
		}
		else
		{
			copy_size = MIDI_EVENT_DATA_SIZE;
		}

		midi_event_buffer[midi_event_buffer_write_index].type = type;
		memcpy(midi_event_buffer[midi_event_buffer_write_index].data, data, copy_size);
		midi_event_buffer_write_index = next_write_index;
	}

	pthread_mutex_unlock(&midi_buffer_lock);
}

midi_event_t *midi_pop_event(midi_event_t *event)
{
	midi_event_t *return_event = NULL;

	pthread_mutex_lock(&midi_buffer_lock);

	if (midi_event_buffer_read_index != midi_event_buffer_write_index) {
		memcpy(event, &midi_event_buffer[midi_event_buffer_read_index], sizeof(midi_event_t));
		return_event = event;
		midi_event_buffer_read_index = (midi_event_buffer_read_index + 1) & MIDI_EVENT_BUFFER_MASK;
	}
	pthread_mutex_unlock(&midi_buffer_lock);

	return return_event;
}

int midi_get_event_count()
{
	int event_count;

	pthread_mutex_lock(&midi_buffer_lock);

	if (midi_event_buffer_read_index <= midi_event_buffer_write_index)
	{
		event_count = midi_event_buffer_write_index - midi_event_buffer_read_index;
	}
	else
	{
		event_count = MIDI_EVENT_BUFFER_SIZE - midi_event_buffer_read_index + midi_event_buffer_write_index;
	}

	pthread_mutex_unlock(&midi_buffer_lock);

	return event_count;
}

static void midi_read_packet(int handle)
{
	unsigned char control_byte;
	if (read(handle, &control_byte, 1) < 1)
	{
		return;
	}

	if ((control_byte & 0xf0) == 0xb0)
	{
		unsigned char control_data[2];

		if (read(handle, &control_data, sizeof(control_data)) == sizeof(control_data))
		{
			int channel_index = control_byte & 0x0f;
			channel_data* channel = &channels[channel_index];

			int control_index = control_data[0];
			if (control_index < channel->controller_count)
			{
				channel->controller_data[control_index] = control_data[1];
				channel->controller_flag[control_index] = 1;
			}
		}
	}
	else if (control_byte == 0x80 || control_byte == 0x90)
	{
		unsigned char control_data[2];
		if (read(handle, &control_data, sizeof(control_data)) == sizeof(control_data))
		{
			midi_push_event(control_byte, sizeof(control_data), control_data);
		}
	}
}

static void* midi_thread()
{
	while (1)
	{
		fd_set rfds;
		int nfds = midi_handle1 + 1;

        FD_ZERO(&rfds);
        FD_SET(midi_handle1, &rfds);
        if (midi_handle2 != -1)
        {
            FD_SET(midi_handle2, &rfds);
            if (midi_handle2 > midi_handle1)
            {
            	nfds = midi_handle2 + 1;
            }
        }

        if (select(nfds, &rfds, NULL, NULL, NULL) == -1)
        {
        	break;
        }

        if (FD_ISSET(midi_handle1, &rfds))
        {
        	midi_read_packet(midi_handle1);
        }

        if (FD_ISSET(midi_handle2, &rfds))
        {
        	midi_read_packet(midi_handle2);
        }
	}

	return NULL;
}

int midi_initialise(const char* device1_name, const char* device2_name)
{
	for (int i = 0; i < CHANNEL_COUNT; i++)
	{
		channels[i].controller_data = (char*) calloc(channels[i].controller_count, 1);
		channels[i].controller_flag = (char*) calloc(channels[i].controller_count, 1);
	}

	midi_handle1 = open(device1_name, O_RDONLY);
	if (midi_handle1 == -1)
	{
		printf("Midi error: cannot open device %s\n", device1_name);
		return -1;
	}

	if (device2_name != NULL)
	{
		midi_handle2 = open(device2_name, O_RDONLY);
		if (midi_handle2 == -1)
		{
			printf("Midi error: cannot open device %s\n", device2_name);
			return -1;
		}
	}

	pthread_create(&midi_thread_handle, NULL, midi_thread, NULL);

	return 0;
}

int midi_get_controller_changed(int channel_index, int controller_index)
{
	int controller_changed = 0;

	if (channel_index >= 0 && channel_index < CHANNEL_COUNT)
	{
		channel_data* channel = &channels[channel_index];

		if (controller_index >= 0 && controller_index < channel->controller_count)
		{
			controller_changed = channel->controller_flag[controller_index];
			channel->controller_flag[controller_index] = 0;
		}
	}

	return controller_changed;
}

int midi_get_controller_value(int channel_index, int controller_index)
{
	int controller_value = -1;

	if (channel_index >= 0 && channel_index < CHANNEL_COUNT)
	{
		channel_data* channel = &channels[channel_index];

		if (controller_index >= 0 && controller_index < channel->controller_count)
		{
			controller_value = channel->controller_data[controller_index];
		}
	}

	return controller_value;
}

void midi_deinitialise()
{
	pthread_cancel(midi_thread_handle);
	pthread_join(midi_thread_handle, NULL);

	if (midi_handle2 != -1)
	{
		close(midi_handle2);
	}
	close(midi_handle1);

	for (int i = 0; i < CHANNEL_COUNT; i++)
	{
		free(channels[i].controller_data);
		channels[i].controller_data = NULL;
	}
}

int32_t midi_get_note_frequency(int midi_note)
{
	if (midi_note < 0)
	{
		midi_note = 0;
	}
	else if (midi_note >= MIDI_NOTE_COUNT)
	{
		midi_note = MIDI_NOTE_COUNT -1;
	}
	return note_frequency[midi_note];
}

int midi_get_note_wavelength_samples(int midi_note)
{
	int32_t note_frequency = midi_get_note_frequency(midi_note);
	int64_t sample_rate = (int64_t)SYSTEM_SAMPLE_RATE << (FIXED_PRECISION * 2);
	int64_t wavelength = (sample_rate / note_frequency) + (int64_t)FIXED_HALF;
	return (int)(wavelength >> FIXED_PRECISION);
}
