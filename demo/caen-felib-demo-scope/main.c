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
*	\brief		CAEN Open FPGA Digitzers Scope demo
*	\author		Giovanni Cerretani
*
******************************************************************************/

#include <assert.h>
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
#define COMMAND_PLOT_WAVE		'w'
#define MAX_NUMBER_OF_SAMPLES	(1U << 10)
#define TIMEOUT_MS				(100)
#define WAVE_FILE_NAME			"Wave.txt"
#define WAVE_FILE_ENABLED		false
#define EVT_FILE_NAME			"EventInfo.txt"
#define EVT_FILE_ENABLED		false
#define DATA_FORMAT " \
	[ \
		{ \"name\" : \"TIMESTAMP\", \"type\" : \"U64\" }, \
		{ \"name\" : \"TRIGGER_ID\", \"type\" : \"U32\" }, \
		{ \"name\" : \"WAVEFORM\", \"type\" : \"U16\", \"dim\" : 2 }, \
		{ \"name\" : \"WAVEFORM_SIZE\", \"type\" : \"SIZE_T\", \"dim\" : 1 }, \
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
	char value[256];
	char par_name[256];

	snprintf(par_name, sizeof(par_name), "/ch/0..%zu/par/ChEnable", n_channels - 1);
	ret = CAEN_FELib_SetValue(dev_handle, par_name, "true");
	if (ret != CAEN_FELib_Success) return ret;

	const unsigned int record_length = 1024; // in samples
	snprintf(value, sizeof(value), "%u", record_length);
	ret = CAEN_FELib_SetValue(dev_handle, "/par/RecordLengthS", value);
	if (ret != CAEN_FELib_Success) return ret;

	assert(record_length <= MAX_NUMBER_OF_SAMPLES);

	const unsigned int pre_trigger = 100; // in samples
	snprintf(value, sizeof(value), "%u", pre_trigger);
	ret = CAEN_FELib_SetValue(dev_handle, "/par/PreTriggerS", value);
	if (ret != CAEN_FELib_Success) return ret;

	ret = CAEN_FELib_SetValue(dev_handle, "/par/AcqTriggerSource", "SwTrg | TestPulse");
	if (ret != CAEN_FELib_Success) return ret;

	const double pulse_period = 100e6; // in nsec
	snprintf(value, sizeof(value), "%f", pulse_period);
	ret = CAEN_FELib_SetValue(dev_handle, "/par/TestPulsePeriod", value);
	if (ret != CAEN_FELib_Success) return ret;

	const unsigned int pulse_width = 1000; // in nsec
	snprintf(value, sizeof(value), "%u", pulse_width);
	ret = CAEN_FELib_SetValue(dev_handle, "/par/TestPulseWidth", value);
	if (ret != CAEN_FELib_Success) return ret;

	const double dc_offset = 50.; // in percent
	snprintf(value, sizeof(value), "%f", dc_offset);
	snprintf(par_name, sizeof(par_name), "/ch/0..%zu/par/DCOffset", n_channels - 1);
	ret = CAEN_FELib_SetValue(dev_handle, par_name, value);
	if (ret != CAEN_FELib_Success) return ret;

	return ret;

}

static int configure_endpoint(uint64_t ep_handle) {
	int ret;
	// conigure endpoint
	uint64_t ep_folder_handle;
	ret = CAEN_FELib_GetParentHandle(ep_handle, NULL, &ep_folder_handle);
	if (ret != CAEN_FELib_Success) return ret;
	ret = CAEN_FELib_SetValue(ep_folder_handle, "/par/activeendpoint", "scope");
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
	uint64_t timestamp;
	double timestamp_us;
	uint32_t trigger_id;
	size_t event_size;
	uint16_t** waveform;
	size_t* n_samples;
	size_t* n_allocated_samples;
	size_t n_channels;
};

struct acq_data {
	uint64_t dev_handle;
	mtx_t mtx;
	cnd_t cnd;
	bool ep_configured;
	bool acq_started;
	size_t n_channels;
	bool plot_next_wave;
};

struct plotters {
	FILE* gnuplot_w;
};

static void print_stats(double t, size_t n_events, double rate) {
	printf("\x1b[1K\rTime (s): %.1f\tEvents: %zu\tReadout rate (MB/s): %f", t, n_events, rate);
	fflush(stdout);
}

static void plot_waveform(FILE* gnuplot, struct event* evt) {
	FILE* f_wave = fopen(WAVE_FILE_NAME, "w");
	if (f_wave == NULL) {
		fprintf(stderr, "fopen failed");
		thrd_exit(EXIT_FAILURE);
	}
	// get max n_samples
	size_t max_n_samples = 0;
	for (size_t ch = 0; ch < evt->n_channels; ++ch) {
		if (evt->n_samples[ch] > max_n_samples)
			max_n_samples = evt->n_samples[ch];
	}
	// title
	for (size_t ch = 0; ch < evt->n_channels; ++ch) {
		const bool last_ch = (ch == evt->n_channels - 1);
		fprintf(f_wave, "CH%zu%c", ch, last_ch ? '\n' : '\t');
	}
	// waveform
	for (size_t i = 0; i < max_n_samples; ++i) {
		for (size_t ch = 0; ch < evt->n_channels; ++ch) {
			const bool last_ch = (ch == evt->n_channels - 1);
			fprintf(f_wave, "%"PRIu16"%c", evt->waveform[ch][i], last_ch ? '\n' : '\t');
		}
	}
	fclose(f_wave);
	fprintf(gnuplot, "set title 'Waveform (timestamp %.3f us)'\n", evt->timestamp_us);
	for (size_t i = 0; i < evt->n_channels; ++i) {
		const bool first_ch = (i == 0);
		const bool last_ch = (i == evt->n_channels - 1);
		if (first_ch)
			fprintf(gnuplot, "plot '%s' using %zu with step", WAVE_FILE_NAME, i + 1);
		else
			fprintf(gnuplot, ",      '' using %zu with step", i + 1);
		if (last_ch)
			fputc('\n', gnuplot);
	}
	fflush(gnuplot);
}

static void save_event(FILE* f_evt, FILE* f_wave, struct event* evt) {

	const bool save_event = f_evt != NULL;
	const bool save_wave = f_wave != NULL;
	const bool save_enabled = save_event || save_wave;

	if (save_enabled) {
		char str[256];
		snprintf(str, sizeof(str), "ts: %.3f us\t\ttrg_id: %"PRIu32"\t\tnum_samples: %zu\n", evt->timestamp_us, evt->trigger_id, evt->n_samples[0]);
		if (save_event) {
			fputs(str, f_evt);
		}
		if (save_wave) {
			fputs(str, f_wave);
			for (size_t i = 0; i < evt->n_channels; ++i) {
				const size_t ch_size = evt->n_samples[i];
				if (ch_size > 0) {
					fprintf(f_wave, "CH_%zu\n", i);
					for (size_t s = 0; s < ch_size; ++s)
						fprintf(f_wave, "%"PRIu16"\n", evt->waveform[i][s]);
				}
			}
		}
	}

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

static void read_data_loop(struct plotters* plotters, FILE* f_evt, FILE* f_wave, struct acq_data* acq_data, uint64_t ep_handle, struct event* evt) {

	struct counters total;
	struct counters interval;

	counters_reset(&total, time(NULL));
	counters_reset(&interval, total.t_begin);

	for (;;) {

		const time_t current_time = time(NULL);
		const double dt = counters_dt(&interval, current_time);
		if (dt >= 1.) {
			// print stats
			print_stats(counters_dt(&total, current_time), total.n_events, counters_rate(&interval, current_time));
			counters_reset(&interval, current_time);
		}

		const int ret = CAEN_FELib_ReadData(ep_handle, TIMEOUT_MS,
			&evt->timestamp,
			&evt->trigger_id,
			evt->waveform,
			evt->n_samples,
			&evt->event_size
		);
		switch (ret) {
		case CAEN_FELib_Success: {

			counters_increment(&total, evt->event_size);
			counters_increment(&interval, evt->event_size);

			evt->timestamp_us = evt->timestamp * .008;

			save_event(f_evt, f_wave, evt);

			mtx_lock(&acq_data->mtx);
			if (acq_data->plot_next_wave) {
				acq_data->plot_next_wave = false;
				plot_waveform(plotters->gnuplot_w, evt);
			}
			mtx_unlock(&acq_data->mtx);

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

static struct event* allocate_event(size_t n_samples, size_t n_channels) {
	struct event* evt = malloc(sizeof(*evt));
	if (evt == NULL) {
		fprintf(stderr, "malloc failed");
		thrd_exit(EXIT_FAILURE);
	}
	evt->n_channels = n_channels;
	evt->n_samples = malloc(evt->n_channels * sizeof(*evt->n_samples));
	if (evt->n_samples == NULL) {
		fprintf(stderr, "malloc failed");
		thrd_exit(EXIT_FAILURE);
	}
	evt->n_allocated_samples = malloc(evt->n_channels * sizeof(*evt->n_allocated_samples));
	if (evt->n_allocated_samples == NULL) {
		fprintf(stderr, "malloc failed");
		thrd_exit(EXIT_FAILURE);
	}
	evt->waveform = malloc(evt->n_channels * sizeof(*evt->waveform));
	if (evt->waveform == NULL) {
		fprintf(stderr, "malloc failed");
		thrd_exit(EXIT_FAILURE);
	}
	for (size_t i = 0; i < evt->n_channels; ++i) {
		evt->n_allocated_samples[i] = n_samples;
		evt->waveform[i] = malloc(evt->n_allocated_samples[i] * sizeof(*evt->waveform[i]));
		if (evt->waveform[i] == NULL) {
			fprintf(stderr, "malloc failed");
			thrd_exit(EXIT_FAILURE);
		}
	}
	return evt;
}

static void free_event(struct event* evt) {
	for (size_t i = 0; i < evt->n_channels; ++i)
		free(evt->waveform[i]);
	free(evt->waveform);
	free(evt->n_allocated_samples);
	free(evt->n_samples);
	free(evt);
}

static struct plotters* open_plotters() {
	struct plotters* plotters = malloc(sizeof(*plotters));
	if (plotters == NULL) {
		fprintf(stderr, "malloc failed");
		thrd_exit(EXIT_FAILURE);
	}
	plotters->gnuplot_w = _popen(GNUPLOT, "w");
	if (plotters->gnuplot_w == NULL) {
		fprintf(stderr, "popen failed");
		thrd_exit(EXIT_FAILURE);
	}

	fprintf(plotters->gnuplot_w, "set key autotitle columnheader\n");
	fprintf(plotters->gnuplot_w, "set xlabel 'Samples'\n");
	fprintf(plotters->gnuplot_w, "set ylabel 'ADC counts'\n");
	fprintf(plotters->gnuplot_w, "set grid\nset mouse\n");
	fprintf(plotters->gnuplot_w, "set key samplen 1 spacing 1\n");
	fflush(plotters->gnuplot_w);

	return plotters;
}

static void close_plotters(struct plotters* plotters) {
	_pclose(plotters->gnuplot_w);
}

static int acq_thread(void* p) {

	int ret;

	struct acq_data* data = (struct acq_data*)p;
	uint64_t ep_handle;

	ret = CAEN_FELib_GetHandle(data->dev_handle, "/endpoint/scope", &ep_handle);
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
	struct event* evt = allocate_event(MAX_NUMBER_OF_SAMPLES, data->n_channels);
	struct plotters* plt = open_plotters();

	FILE* f_evt = NULL;
	FILE* f_wave = NULL;

	if (EVT_FILE_ENABLED) {
		f_evt = fopen(EVT_FILE_NAME, "w");
		if (f_evt == NULL) {
			fprintf(stderr, "fopen failed");
			return EXIT_FAILURE;
		}
	}

	if (WAVE_FILE_ENABLED) {
		f_wave = fopen(WAVE_FILE_NAME, "w");
		if (f_wave == NULL) {
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
	read_data_loop(plt, f_evt, f_wave, data, ep_handle, evt);

	// quit
	if (f_evt != NULL)
		fclose(f_evt);
	if (f_wave != NULL)
		fclose(f_wave);
	close_plotters(plt);
	free_event(evt);

	return EXIT_SUCCESS;
}

int main(int argc, char* argv[]) {

	int ret;

	uint64_t dev_handle;

	printf("##########################################\n");
	printf("\tCAEN firmware Scope demo\n");
	printf("##########################################\n");

	if (argc > 2) {
		fputs("invalid arguments", stderr);
		return EXIT_FAILURE;
	}

	// select device
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
		.n_channels = n_channels
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

	printf("done.\n");

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
