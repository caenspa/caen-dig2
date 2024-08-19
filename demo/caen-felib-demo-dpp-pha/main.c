/******************************************************************************
*
*	CAEN SpA - Software Division
*	Via Vetraia, 11 - 55049 - Viareggio ITALY
*	+39 0594 388 398 - www.caen.it
*
*******************************************************************************
*
*	Copyright (C) 2020-2023 CAEN SpA
*
*	This file is part of the CAEN Dig2 Library.
*
*	Permission is hereby granted, free of charge, to any person obtaining a
*	copy of this software and associated documentation files (the "Software"),
*	to deal in the Software without restriction, including without limitation
*	the rights to use, copy, modify, merge, publish, distribute, sublicense,
*	and/or sell copies of the Software, and to permit persons to whom the
*	Software is furnished to do so.
*
*	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
*	THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
*	FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
*	DEALINGS IN THE SOFTWARE.
*
*	SPDX-License-Identifier: MIT-0
*
***************************************************************************//*!
*
*	\file		main.c
*	\brief		CAEN Open FPGA Digitzers DPP-PHA demo
*	\author		Giovanni Cerretani, Alberto Potenza
*
******************************************************************************/

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// windows or C11
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L) && !defined(__STDC_NO_THREADS__)
#include <threads.h>
#else
#include "c11threads.h"
#endif

#include <CAEN_FELib.h>

// Basic hardcoded configuration
#define COMMAND_TRIGGER			't'
#define COMMAND_STOP			'q'
#define COMMAND_INCR_CH			'+'
#define COMMAND_DECR_CH			'-'
#define COMMAND_PLOT_WAVE		'w'
#define MAX_NUMBER_OF_SAMPLES	(4095 * 2)
#define MAX_NUMBER_OF_BINS		(1U << 16)
#define EVT_FILE_NAME			"EventInfo.txt"
#define EVT_FILE_ENABLED		true
#define HISTO_FILE_NAME			"Histogram_%zu.txt"
#define WAVE_FILE_NAME			"Waveform.txt"
#define DATA_FORMAT " \
	[ \
		{ \"name\" : \"CHANNEL\", \"type\" : \"U8\" }, \
		{ \"name\" : \"TIMESTAMP\", \"type\" : \"U64\" }, \
		{ \"name\" : \"FINE_TIMESTAMP\", \"type\" : \"U16\" }, \
		{ \"name\" : \"ENERGY\", \"type\" : \"U16\" }, \
		{ \"name\" : \"ANALOG_PROBE_1\", \"type\" : \"I32\", \"dim\" : 1 }, \
		{ \"name\" : \"ANALOG_PROBE_2\", \"type\" : \"I32\", \"dim\" : 1 }, \
		{ \"name\" : \"DIGITAL_PROBE_1\", \"type\" : \"U8\", \"dim\" : 1 }, \
		{ \"name\" : \"DIGITAL_PROBE_2\", \"type\" : \"U8\", \"dim\" : 1 }, \
		{ \"name\" : \"DIGITAL_PROBE_3\", \"type\" : \"U8\", \"dim\" : 1 }, \
		{ \"name\" : \"DIGITAL_PROBE_4\", \"type\" : \"U8\", \"dim\" : 1 }, \
		{ \"name\" : \"ANALOG_PROBE_1_TYPE\", \"type\" : \"U8\" }, \
		{ \"name\" : \"ANALOG_PROBE_2_TYPE\", \"type\" : \"U8\" }, \
		{ \"name\" : \"DIGITAL_PROBE_1_TYPE\", \"type\" : \"U8\" }, \
		{ \"name\" : \"DIGITAL_PROBE_2_TYPE\", \"type\" : \"U8\" }, \
		{ \"name\" : \"DIGITAL_PROBE_3_TYPE\", \"type\" : \"U8\" }, \
		{ \"name\" : \"DIGITAL_PROBE_4_TYPE\", \"type\" : \"U8\" }, \
		{ \"name\" : \"WAVEFORM_SIZE\", \"type\" : \"SIZE_T\" }, \
		{ \"name\" : \"FLAGS_LOW_PRIORITY\", \"type\" : \"U16\"}, \
		{ \"name\" : \"FLAGS_HIGH_PRIORITY\", \"type\" : \"U16\" }, \
		{ \"name\" : \"EVENT_SIZE\", \"type\" : \"SIZE_T\" } \
	] \
"

// Utilities
#define ARRAY_SIZE(X)		(sizeof(X)/sizeof((X)[0]))

#ifdef _WIN32 // Windows
#include <conio.h>
#define GNUPLOT						"pgnuplot.exe"
#else // Linux
#include <unistd.h>
#include <termios.h>
#define GNUPLOT						"gnuplot"
#define _popen(command, type)		popen(command, type)
#define _pclose(command)			pclose(command)
// Linux replacement for non standard _getch
static int _getch() {
	struct termios oldattr;
	if (tcgetattr(STDIN_FILENO, &oldattr) == -1) perror(NULL);
	struct termios newattr = oldattr;
	newattr.c_lflag &= ~(ICANON | ECHO);
	if (tcsetattr(STDIN_FILENO, TCSANOW, &newattr) == -1) perror(NULL);
	const int ch = getchar();
	if (tcsetattr(STDIN_FILENO, TCSANOW, &oldattr) == -1) perror(NULL);
	return ch;
}
#endif

static unsigned long long value_to_ull(const char* value) {
	char* value_end;
	const unsigned long long ret = strtoull(value, &value_end, 0);
	if (value == value_end || errno == ERANGE)
		fprintf(stderr, "strtoull error\n");
	return ret;
}

static double value_to_d(const char* value) {
	char* value_end;
	const double ret = strtod(value, &value_end);
	if (value == value_end || errno == ERANGE)
		fprintf(stderr, "strtod error\n");
	return ret;
}

static int print_last_error(void) {
	char msg[1024];
	int ec = CAEN_FELib_GetLastError(msg);
	if (ec != CAEN_FELib_Success) {
		fprintf(stderr, "%s failed\n", __func__);
		return ec;
	}
	fprintf(stderr, "last error: %s\n", msg);
	return ec;
}

static int connect_to_digitizer(uint64_t* dev_handle, int argc, char* argv[]) {
	const char* path;
	char local_path[256];
	printf("device path: ");
	if (argc == 2) {
		path = argv[1];
		puts(path);
	} else {
		while (fgets(local_path, sizeof(local_path), stdin) == NULL);
		local_path[strcspn(local_path, "\r\n")] = '\0'; // remove newline added by fgets
		path = local_path;
	}
	return CAEN_FELib_Open(path, dev_handle);
}

static int get_n_channels(uint64_t dev_handle, size_t* n_channels) {
	int ret;
	char value[256];
	ret = CAEN_FELib_GetValue(dev_handle, "/par/NumCh", value);
	if (ret != CAEN_FELib_Success) return ret;
	*n_channels = (size_t)value_to_ull(value);
	return ret;
}

static int print_digitizer_details(uint64_t dev_handle) {
	int ret;
	char value[256];
	ret = CAEN_FELib_GetValue(dev_handle, "/par/ModelName", value);
	if (ret != CAEN_FELib_Success) return ret;
	printf("Model name:\t%s\n", value);
	ret = CAEN_FELib_GetValue(dev_handle, "/par/SerialNum", value);
	if (ret != CAEN_FELib_Success) return ret;
	printf("Serial number:\t%s\n", value);
	ret = CAEN_FELib_GetValue(dev_handle, "/par/ADC_Nbit", value);
	if (ret != CAEN_FELib_Success) return ret;
	printf("ADC bits:\t%llu\n", value_to_ull(value));
	ret = CAEN_FELib_GetValue(dev_handle, "/par/NumCh", value);
	if (ret != CAEN_FELib_Success) return ret;
	printf("Channels:\t%llu\n", value_to_ull(value));
	ret = CAEN_FELib_GetValue(dev_handle, "/par/ADC_SamplRate", value);
	if (ret != CAEN_FELib_Success) return ret;
	printf("ADC rate:\t%f Msps\n", value_to_d(value));
	ret = CAEN_FELib_GetValue(dev_handle, "/par/cupver", value);
	if (ret != CAEN_FELib_Success) return ret;
	printf("CUP version:\t%s\n", value);
	return ret;
}

static int configure_digitizer(uint64_t dev_handle, size_t n_channels) {

	int ret;
	char par_name[256];

	// Channel settings
	snprintf(par_name, sizeof(par_name), "/ch/0..%zu/par/ChEnable", n_channels - 1);
	ret = CAEN_FELib_SetValue(dev_handle, par_name, "true");
	if (ret != CAEN_FELib_Success) return ret;

	// Global trigger configuration
	ret = CAEN_FELib_SetValue(dev_handle, "/par/GlobalTriggerSource", "SwTrg | TestPulse");
	if (ret != CAEN_FELib_Success) return ret;
	ret = CAEN_FELib_SetValue(dev_handle, "/par/TestPulsePeriod", "100000000");
	if (ret != CAEN_FELib_Success) return ret;
	ret = CAEN_FELib_SetValue(dev_handle, "/par/TestPulseWidth", "16");
	if (ret != CAEN_FELib_Success) return ret;

	// Wave configuration
	snprintf(par_name, sizeof(par_name), "/ch/0..%zu/par/ChRecordLengthS", n_channels - 1);
	ret = CAEN_FELib_SetValue(dev_handle, par_name, "512");
	if (ret != CAEN_FELib_Success) return ret;
	snprintf(par_name, sizeof(par_name), "/ch/0..%zu/par/WaveTriggerSource", n_channels - 1);
	ret = CAEN_FELib_SetValue(dev_handle, par_name, "GlobalTriggerSource");
	if (ret != CAEN_FELib_Success) return ret;
	snprintf(par_name, sizeof(par_name), "/ch/0..%zu/par/WaveAnalogProbe0", n_channels - 1);
	ret = CAEN_FELib_SetValue(dev_handle, par_name, "ADCInput");
	if (ret != CAEN_FELib_Success) return ret;
	snprintf(par_name, sizeof(par_name), "/ch/0..%zu/par/WaveAnalogProbe1", n_channels - 1);
	ret = CAEN_FELib_SetValue(dev_handle, par_name, "TimeFilter");
	if (ret != CAEN_FELib_Success) return ret;
	snprintf(par_name, sizeof(par_name), "/ch/0..%zu/par/WaveDigitalProbe0", n_channels - 1);
	ret = CAEN_FELib_SetValue(dev_handle, par_name, "Trigger");
	if (ret != CAEN_FELib_Success) return ret;
	snprintf(par_name, sizeof(par_name), "/ch/0..%zu/par/WaveDigitalProbe1", n_channels - 1);
	ret = CAEN_FELib_SetValue(dev_handle, par_name, "TimeFilterArmed");
	if (ret != CAEN_FELib_Success) return ret;
	snprintf(par_name, sizeof(par_name), "/ch/0..%zu/par/WaveDigitalProbe2", n_channels - 1);
	ret = CAEN_FELib_SetValue(dev_handle, par_name, "EnergyFilterBaselineFreeze");
	if (ret != CAEN_FELib_Success) return ret;
	snprintf(par_name, sizeof(par_name), "/ch/0..%zu/par/WaveDigitalProbe3", n_channels - 1);
	ret = CAEN_FELib_SetValue(dev_handle, par_name, "EnergyFilterPeakReady");
	if (ret != CAEN_FELib_Success) return ret;
	snprintf(par_name, sizeof(par_name), "/ch/0..%zu/par/ChPreTriggerS", n_channels - 1);
	ret = CAEN_FELib_SetValue(dev_handle, par_name, "200");
	if (ret != CAEN_FELib_Success) return ret;

	// Event configuration
	snprintf(par_name, sizeof(par_name), "/ch/0..%zu/par/EventTriggerSource", n_channels - 1);
	ret = CAEN_FELib_SetValue(dev_handle, par_name, "GlobalTriggerSource");
	if (ret != CAEN_FELib_Success) return ret;

	// Filter parameters
	snprintf(par_name, sizeof(par_name), "/ch/0..%zu/par/TimeFilterRiseTimeS", n_channels - 1);
	ret = CAEN_FELib_SetValue(dev_handle, par_name, "10");
	if (ret != CAEN_FELib_Success) return ret;
	snprintf(par_name, sizeof(par_name), "/ch/0..%zu/par/EnergyFilterRiseTimeS", n_channels - 1);
	ret = CAEN_FELib_SetValue(dev_handle, par_name, "100");
	if (ret != CAEN_FELib_Success) return ret;
	snprintf(par_name, sizeof(par_name), "/ch/0..%zu/par/EnergyFilterFlatTopS", n_channels - 1);
	ret = CAEN_FELib_SetValue(dev_handle, par_name, "100");
	if (ret != CAEN_FELib_Success) return ret;
	snprintf(par_name, sizeof(par_name), "/ch/0..%zu/par/TriggerThr", n_channels - 1);
	ret = CAEN_FELib_SetValue(dev_handle, par_name, "3");
	if (ret != CAEN_FELib_Success) return ret;
	snprintf(par_name, sizeof(par_name), "/ch/0..%zu/par/EnergyFilterPeakingPosition", n_channels - 1);
	ret = CAEN_FELib_SetValue(dev_handle, par_name, "80");
	if (ret != CAEN_FELib_Success) return ret;
	snprintf(par_name, sizeof(par_name), "/ch/0..%zu/par/EnergyFilterPoleZeroS", n_channels - 1);
	ret = CAEN_FELib_SetValue(dev_handle, par_name, "1000");
	if (ret != CAEN_FELib_Success) return ret;
	snprintf(par_name, sizeof(par_name), "/ch/0..%zu/par/TimeFilterRetriggerGuardS", n_channels - 1);
	ret = CAEN_FELib_SetValue(dev_handle, par_name, "10");
	if (ret != CAEN_FELib_Success) return ret;
	snprintf(par_name, sizeof(par_name), "/ch/0..%zu/par/EnergyFilterPileupGuardT", n_channels - 1);
	ret = CAEN_FELib_SetValue(dev_handle, par_name, "10");
	if (ret != CAEN_FELib_Success) return ret;
	snprintf(par_name, sizeof(par_name), "/ch/0..%zu/par/EnergyFilterBaselineGuardS", n_channels - 1);
	ret = CAEN_FELib_SetValue(dev_handle, par_name, "100");
	if (ret != CAEN_FELib_Success) return ret;
	snprintf(par_name, sizeof(par_name), "/ch/0..%zu/par/PulsePolarity", n_channels - 1);
	ret = CAEN_FELib_SetValue(dev_handle, par_name, "Positive");
	if (ret != CAEN_FELib_Success) return ret;
	snprintf(par_name, sizeof(par_name), "/ch/0..%zu/par/EnergyFilterLFLimitation", n_channels - 1);
	ret = CAEN_FELib_SetValue(dev_handle, par_name, "Off");
	if (ret != CAEN_FELib_Success) return ret;
	snprintf(par_name, sizeof(par_name), "/ch/0..%zu/par/EnergyFilterBaselineAvg", n_channels - 1);
	ret = CAEN_FELib_SetValue(dev_handle, par_name, "Medium");
	if (ret != CAEN_FELib_Success) return ret;
	snprintf(par_name, sizeof(par_name), "/ch/0..%zu/par/EnergyFilterFineGain", n_channels - 1);
	ret = CAEN_FELib_SetValue(dev_handle, par_name, "1.0");
	if (ret != CAEN_FELib_Success) return ret;

	return ret;

}

static int configure_endpoint(uint64_t ep_handle) {
	int ret;
	// conigure endpoint
	uint64_t ep_folder_handle;
	ret = CAEN_FELib_GetParentHandle(ep_handle, NULL, &ep_folder_handle);
	if (ret != CAEN_FELib_Success) return ret;
	ret = CAEN_FELib_SetValue(ep_folder_handle, "/par/activeendpoint", "dpppha");
	if (ret != CAEN_FELib_Success) return ret;
	ret = CAEN_FELib_SetReadDataFormat(ep_handle, DATA_FORMAT);
	if (ret != CAEN_FELib_Success) return ret;
	return ret;
}

struct counters {
	size_t total_size;
	size_t n_events;
	time_t t_begin;
};

struct event {
	uint8_t channel;
	uint64_t timestamp;
	double timestamp_us;
	uint16_t fine_timestamp;
	uint16_t energy;
	uint16_t flags_low_priority;
	uint16_t flags_high_priority;
	size_t event_size;
	int32_t* analog_probes[2];
	uint8_t* digital_probes[4];
	uint8_t analog_probes_type[2];
	uint8_t digital_probes_type[4];
	size_t n_allocated_samples;
	size_t n_samples;
};

struct histograms {
	uint32_t** histogram;
	size_t n_allocated_channels;
	size_t n_allocated_bins;
};

struct acq_data {
	uint64_t dev_handle;
	mtx_t mtx;
	cnd_t cnd;
	bool ep_configured;
	bool acq_started;
	size_t n_channels;
	size_t active_channel;
	bool plot_next_wave;
};

struct plotters {
	FILE* gnuplot_h;
	FILE* gnuplot_w;
};

static void print_stats(double t, size_t n_events, double rate) {
	printf("\x1b[1K\rTime (s): %.1f\tEvents: %zu\tReadout rate (MB/s): %f", t, n_events, rate);
	fflush(stdout);
}

static void plot_histogram(FILE* gnuplot, size_t channel, struct histograms* h) {
	char filename[64];
	snprintf(filename, ARRAY_SIZE(filename), HISTO_FILE_NAME, channel);
	FILE* f_histo = fopen(filename, "w");
	if (f_histo == NULL) {
		fprintf(stderr, "fopen failed");
		thrd_exit(EXIT_FAILURE);
	}
	uint32_t* hc = h->histogram[channel];
	for (size_t i = 0; i < h->n_allocated_bins; ++i)
		fprintf(f_histo, "%"PRIu32"\n", hc[i]);
	fclose(f_histo);
	fprintf(gnuplot, "set title 'Histogram (channel %zu)'\n", channel);
	fprintf(gnuplot, "plot '%s' with step\n", filename);
	fflush(gnuplot);
}

static const char* digital_probe_type(uint8_t type) {
	switch (type) {
	case 0:		return "trigger";
	case 1:		return "time_filter_armed";
	case 2:		return "re_trigger_guard";
	case 3:		return "energy_filter_baseline_freeze";
	case 4:		return "energy_filter_peaking";
	case 5:		return "energy_filter_peak_ready";
	case 6:		return "energy_filter_pile_up_guard";
	case 7:		return "event_pile_up";
	case 8:		return "adc_saturation";
	case 9:		return "adc_saturation_protection";
	case 10:	return "post_saturation_event";
	case 11:	return "energy_filter_saturation";
	case 12:	return "signal_inhibit";
	default:	return "UNKNOWN";
	}
}

static const char* analog_probe_type(uint8_t type) {
	switch (type) {
	case 0:		return "adc_input";
	case 1:		return "time_filter";
	case 2:		return "energy_filter";
	case 3:		return "energy_filter_baseline";
	case 4:		return "energy_filter_minus_baseline";
	default:	return "UNKNOWN";
	}
}

static void plot_waveform(FILE* gnuplot, struct event* evt) {
	FILE* f_wave = fopen(WAVE_FILE_NAME, "w");
	if (f_wave == NULL) {
		fprintf(stderr, "fopen failed");
		thrd_exit(EXIT_FAILURE);
	}
	fprintf(f_wave, "'%s'\t'%s'\t'%s'\t'%s'\t'%s'\t'%s'\n",
		analog_probe_type(evt->analog_probes_type[0]),
		analog_probe_type(evt->analog_probes_type[1]),
		digital_probe_type(evt->digital_probes_type[0]),
		digital_probe_type(evt->digital_probes_type[1]),
		digital_probe_type(evt->digital_probes_type[2]),
		digital_probe_type(evt->digital_probes_type[3])
	);
	for (size_t i = 0; i < evt->n_samples; ++i)
		fprintf(f_wave, "%"PRIi32"\t%"PRIi32"\t%"PRIu8"\t%"PRIu8"\t%"PRIu8"\t%"PRIu8"\n",
			evt->analog_probes[0][i],
			evt->analog_probes[1][i],
			evt->digital_probes[0][i],
			evt->digital_probes[1][i],
			evt->digital_probes[2][i],
			evt->digital_probes[3][i]
		);
	fclose(f_wave);
	fprintf(gnuplot, "set title 'Waveform (channel %"PRIu8", timestamp %.3f us)'\n", evt->channel, evt->timestamp_us);
	fprintf(gnuplot, "plot '%s' using 1 with step", WAVE_FILE_NAME);
	fprintf(gnuplot, ",      '' using 2 with step");
	fprintf(gnuplot, ",      '' using (1000*$3 - 1100) with step");
	fprintf(gnuplot, ",      '' using (1000*$4 - 2200) with step");
	fprintf(gnuplot, ",      '' using (1000*$5 - 3300) with step");
	fprintf(gnuplot, ",      '' using (1000*$6 - 4400) with step\n");
	fflush(gnuplot);
}

static void save_event(FILE* f_evt, struct event* evt) {
	const bool save_event = f_evt != NULL;
	if (save_event)
		fprintf(f_evt, "%"PRIu8"\t%.3f\t%"PRIu32"\n", evt->channel, evt->timestamp_us, evt->energy);
}

static void fill_histogram(struct event* evt, struct histograms* h) {
	/*
	 * fill histogram
	 * flags_high_priority bits:
	 * 0 or 1:	pileup
	 * 2 or 3:	adc saturation
	 * 4:		energy ovverrange
	 */

	// save events without these flags
	if ((evt->flags_high_priority & 0x1f) == 0)
		++h->histogram[evt->channel][evt->energy];

	// if trapsat is set, fill histogram last bin
	else if (evt->flags_high_priority & 0x10)
		++h->histogram[evt->channel][h->n_allocated_bins - 1];
}

static double counters_dt(struct counters* c, time_t t) {
	return difftime(t, c->t_begin);
}

static double counters_rate(struct counters* c, time_t t) {
	return c->total_size / counters_dt(c, t) / (1024. * 1024.); // MB/s
}

static void counters_increment(struct counters* c, size_t size) {
	c->total_size += size;
	++c->n_events;
}

static void counters_reset(struct counters* c, time_t t) {
	c->total_size = 0;
	c->n_events = 0;
	c->t_begin = t;
}

static void read_data_loop(struct plotters* plotters, FILE* f_evt, struct acq_data* acq_data, uint64_t ep_handle, struct event* evt, struct histograms* h) {

	struct counters total;
	struct counters interval;

	counters_reset(&total, time(NULL));
	counters_reset(&interval, total.t_begin);

	for (;;) {
		const time_t current_time = time(NULL);
		if (counters_dt(&interval, current_time) >= 1.) {
			// print stats
			print_stats(counters_dt(&total, current_time), total.n_events, counters_rate(&interval, current_time));
			counters_reset(&interval, current_time);
			// plot histograms
			mtx_lock(&acq_data->mtx);
			plot_histogram(plotters->gnuplot_h, acq_data->active_channel, h);
			mtx_unlock(&acq_data->mtx);
		}

		const int ret = CAEN_FELib_ReadData(ep_handle, 100,
			&evt->channel,
			&evt->timestamp,
			&evt->fine_timestamp,
			&evt->energy,
			evt->analog_probes[0],
			evt->analog_probes[1],
			evt->digital_probes[0],
			evt->digital_probes[1],
			evt->digital_probes[2],
			evt->digital_probes[3],
			&evt->analog_probes_type[0],
			&evt->analog_probes_type[1],
			&evt->digital_probes_type[0],
			&evt->digital_probes_type[1],
			&evt->digital_probes_type[2],
			&evt->digital_probes_type[3],
			&evt->n_samples,
			&evt->flags_low_priority,
			&evt->flags_high_priority,
			&evt->event_size
		);
		switch (ret) {
		case CAEN_FELib_Success: {

			evt->timestamp_us = evt->timestamp * .008;

			counters_increment(&total, evt->event_size);
			counters_increment(&interval, evt->event_size);
			
			fill_histogram(evt, h);
			save_event(f_evt, evt);

			const bool has_waveform = evt->n_samples > 0;

			if (has_waveform) {
				mtx_lock(&acq_data->mtx);
				if (acq_data->plot_next_wave) {
					acq_data->plot_next_wave = false;
					plot_waveform(plotters->gnuplot_w, evt);
				}
				mtx_unlock(&acq_data->mtx);
			}

			break;
		}
		case CAEN_FELib_Timeout:
			break;
		case CAEN_FELib_Stop:
			printf("\nStop received.\n");
			return;
		default:
			print_last_error();
			break;
		}
	}
}

static struct event* allocate_event(size_t n_samples) {
	struct event* evt = malloc(sizeof(*evt));
	if (evt == NULL) {
		fprintf(stderr, "malloc failed");
		thrd_exit(EXIT_FAILURE);
	}
	evt->n_allocated_samples = n_samples;
	evt->n_samples = 0;
	for (size_t i = 0; i < ARRAY_SIZE(evt->analog_probes); ++i) {
		evt->analog_probes[i] = malloc(evt->n_allocated_samples * sizeof(*evt->analog_probes[i]));
		if (evt->analog_probes[i] == NULL) {
			fprintf(stderr, "malloc failed");
			thrd_exit(EXIT_FAILURE);
		}
	}
	for (size_t i = 0; i < ARRAY_SIZE(evt->digital_probes); ++i) {
		evt->digital_probes[i] = malloc(evt->n_allocated_samples * sizeof(*evt->digital_probes[i]));
		if (evt->digital_probes[i] == NULL) {
			fprintf(stderr, "malloc failed");
			thrd_exit(EXIT_FAILURE);
		}
	}
	return evt;
}

static void free_event(struct event* evt) {
	for (size_t i = 0; i < ARRAY_SIZE(evt->analog_probes); ++i)
		free(evt->analog_probes[i]);
	for (size_t i = 0; i < ARRAY_SIZE(evt->digital_probes); ++i)
		free(evt->digital_probes[i]);
	free(evt);
}

static struct histograms* allocate_histograms(size_t n_channels) {
	struct histograms* histograms = malloc(sizeof(*histograms));
	if (histograms == NULL) {
		fprintf(stderr, "malloc failed");
		thrd_exit(EXIT_FAILURE);
	}
	histograms->n_allocated_channels = n_channels;
	histograms->n_allocated_bins = MAX_NUMBER_OF_BINS;
	histograms->histogram = malloc(histograms->n_allocated_channels * sizeof(*histograms->histogram));
	if (histograms->histogram == NULL) {
		fprintf(stderr, "malloc failed");
		thrd_exit(EXIT_FAILURE);
	}
	for (size_t i = 0; i < histograms->n_allocated_channels; ++i) {
		histograms->histogram[i] = calloc(histograms->n_allocated_bins, sizeof(*histograms->histogram[i]));
		if (histograms->histogram[i] == NULL) {
			fprintf(stderr, "malloc failed");
			thrd_exit(EXIT_FAILURE);
		}
	}
	return histograms;
}

static void free_histograms(struct histograms* h) {
	for (size_t i = 0; i < h->n_allocated_channels; ++i)
		free(h->histogram[i]);
	free(h->histogram);
}

static struct plotters* open_plotters() {
	struct plotters* plotters = malloc(sizeof(*plotters));
	if (plotters == NULL) {
		fprintf(stderr, "malloc failed");
		thrd_exit(EXIT_FAILURE);
	}
	plotters->gnuplot_h = _popen(GNUPLOT, "w");
	if (plotters->gnuplot_h == NULL) {
		fprintf(stderr, "popen failed");
		thrd_exit(EXIT_FAILURE);
	}
	plotters->gnuplot_w = _popen(GNUPLOT, "w");
	if (plotters->gnuplot_w == NULL) {
		fprintf(stderr, "popen failed");
		thrd_exit(EXIT_FAILURE);
	}
	fprintf(plotters->gnuplot_h, "set xlabel 'ADC channels'\n");
	fprintf(plotters->gnuplot_h, "set ylabel 'Counts'\n");
	fprintf(plotters->gnuplot_h, "set grid\nset mouse\n");
	fflush(plotters->gnuplot_h);

	fprintf(plotters->gnuplot_w, "set key autotitle columnheader\n");
	fprintf(plotters->gnuplot_w, "set xlabel 'Samples'\n");
	fprintf(plotters->gnuplot_w, "set ylabel 'ADC counts'\n");
	fprintf(plotters->gnuplot_w, "set grid\nset mouse\n");
	fflush(plotters->gnuplot_w);

	return plotters;
}

static void close_plotters(struct plotters* plotters) {
	_pclose(plotters->gnuplot_h);
	_pclose(plotters->gnuplot_w);
}

static int acq_thread(void* p) {

	int ret;

	struct acq_data* data = (struct acq_data*)p;
	uint64_t ep_handle;

	ret = CAEN_FELib_GetHandle(data->dev_handle, "/endpoint/dpppha", &ep_handle);
	if (ret != CAEN_FELib_Success) {
		print_last_error();
		return EXIT_FAILURE;
	}

	ret = configure_endpoint(ep_handle);
	if (ret != CAEN_FELib_Success) {
		print_last_error();
		return EXIT_FAILURE;
	}

	// allocate stuff
	struct event *evt = allocate_event(MAX_NUMBER_OF_SAMPLES);
	struct histograms* h = allocate_histograms(data->n_channels);
	struct plotters* plt = open_plotters();

	FILE* f_evt = NULL;

	if (EVT_FILE_ENABLED) {
		f_evt = fopen(EVT_FILE_NAME, "w");
		if (f_evt == NULL) {
			fprintf(stderr, "fopen failed");
			return EXIT_FAILURE;
		}
	}

	// signal main thread
	mtx_lock(&data->mtx);
	data->ep_configured = true;
	mtx_unlock(&data->mtx);
	cnd_signal(&data->cnd);

	// wait main thread
	mtx_lock(&data->mtx);
	while (!data->acq_started)
		cnd_wait(&data->cnd, &data->mtx);
	mtx_unlock(&data->mtx);

	// acquisition loop
	read_data_loop(plt, f_evt, data, ep_handle, evt, h);

	// quit
	if (f_evt != NULL)
		fclose(f_evt);
	close_plotters(plt);
	free_event(evt);
	free_histograms(h);

	return EXIT_SUCCESS;
}

int main(int argc, char* argv[]) {

	printf("##########################################\n");
	printf("\tCAEN firmware DPP-PHA demo\n");
	printf("##########################################\n");

	if (argc > 2) {
		fputs("invalid arguments", stderr);
		return EXIT_FAILURE;
	}

	int ret;

	// select device
	uint64_t dev_handle;
	ret = connect_to_digitizer(&dev_handle, argc, argv);
	if (ret != CAEN_FELib_Success) {
		print_last_error();
		return EXIT_FAILURE;
	}

	ret = print_digitizer_details(dev_handle);
	if (ret != CAEN_FELib_Success) {
		print_last_error();
		return EXIT_FAILURE;
	}

	size_t n_channels;
	ret = get_n_channels(dev_handle, &n_channels);
	if (ret != CAEN_FELib_Success) {
		print_last_error();
		return EXIT_FAILURE;
	}

	printf("Resetting...\t");

	// reset
	ret = CAEN_FELib_SendCommand(dev_handle, "/cmd/reset");
	if (ret != CAEN_FELib_Success) {
		print_last_error();
		return EXIT_FAILURE;
	}

	printf("done.\n");

	// start acquisition thread
	struct acq_data data = {
		.dev_handle = dev_handle,
		.ep_configured = false,
		.acq_started = false,
		.n_channels = n_channels,
		.active_channel = 0,
		.plot_next_wave = false,
	};
	mtx_init(&data.mtx, mtx_plain);
	cnd_init(&data.cnd);
	thrd_t thrd;
	ret = thrd_create(&thrd, &acq_thread, &data);
	if (ret != thrd_success) {
		fprintf(stderr, "thrd_create failed");
		return EXIT_FAILURE;
	}

	printf("Configuring...\t");

	ret = configure_digitizer(dev_handle, n_channels);
	if (ret != CAEN_FELib_Success) {
		print_last_error();
		return EXIT_FAILURE;
	}

	// wait configuration on acquisition thread
	mtx_lock(&data.mtx);
	while (!data.ep_configured)
		cnd_wait(&data.cnd, &data.mtx);
	mtx_unlock(&data.mtx);

	printf("done.\n");

	printf("Starting...\t");

	ret = CAEN_FELib_SendCommand(dev_handle, "/cmd/armacquisition");
	if (ret != CAEN_FELib_Success) {
		print_last_error();
		return EXIT_FAILURE;
	}
	ret = CAEN_FELib_SendCommand(dev_handle, "/cmd/swstartacquisition");
	if (ret != CAEN_FELib_Success) {
		print_last_error();
		return EXIT_FAILURE;
	}

	printf("done.\n");

	// notify start to acquisition thread
	mtx_lock(&data.mtx);
	data.acq_started = true;
	mtx_unlock(&data.mtx);
	cnd_signal(&data.cnd);

	printf("##########################################\n");
	printf("Commands supported:\n");
	printf("\t[%c]\tsend manual trigger\n", COMMAND_TRIGGER);
	printf("\t[%c]\tstop acquisition\n", COMMAND_STOP);
	printf("\t[%c]\tincrement channel\n", COMMAND_INCR_CH);
	printf("\t[%c]\tdecrement channel\n", COMMAND_DECR_CH);
	printf("\t[%c]\tplot next waveform\n", COMMAND_PLOT_WAVE);
	printf("##########################################\n");

	bool do_quit = false;

	do {
		const int c = _getch();
		switch (c) {
		case COMMAND_TRIGGER: {
			ret = CAEN_FELib_SendCommand(dev_handle, "/cmd/sendswtrigger");
			if (ret != CAEN_FELib_Success)
				print_last_error();
			break;
		}
		case COMMAND_STOP: {
			do_quit = true;
			break;
		}
		case COMMAND_INCR_CH: {
			mtx_lock(&data.mtx);
			++data.active_channel;
			if (data.active_channel == n_channels)
				data.active_channel = 0;
			mtx_unlock(&data.mtx);
			break;
		}
		case COMMAND_DECR_CH: {
			mtx_lock(&data.mtx);
			if (data.active_channel == 0)
				data.active_channel = n_channels;
			--data.active_channel;
			mtx_unlock(&data.mtx);
			break;
		}
		case COMMAND_PLOT_WAVE: {
			mtx_lock(&data.mtx);
			data.plot_next_wave = true;
			mtx_unlock(&data.mtx);
			break;
		}
		case '\n': {
			break;
		}
		default: {
			fprintf(stderr, "unknown command [%c]", c);
			break;
		}
		}
	} while (!do_quit);

	printf("\nStopping...\t");

	ret = CAEN_FELib_SendCommand(dev_handle, "/cmd/disarmacquisition");
	if (ret != CAEN_FELib_Success) {
		print_last_error();
		return EXIT_FAILURE;
	}

	// wait the end of the acquisition
	// that is going to finish just after the last event
	int thrd_ret;
	ret = thrd_join(thrd, &thrd_ret);
	if (ret != thrd_success || thrd_ret != EXIT_SUCCESS) {
		fprintf(stderr, "thrd_join error.\tret %d\tthrd_ret %d\n", ret, thrd_ret);
		return EXIT_FAILURE;
	}
	mtx_destroy(&data.mtx);
	cnd_destroy(&data.cnd);

	ret = CAEN_FELib_Close(dev_handle);
	if (ret != CAEN_FELib_Success) {
		print_last_error();
		return EXIT_FAILURE;
	}

	printf("\nBye!\n");

	return EXIT_SUCCESS;
}
