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

#include "tlock_queue.h"

#include <CAEN_FELib.h>

// Basic hardcoded configuration
#define COMMAND_TRIGGER			't'
#define COMMAND_STOP			'q'
#define COMMAND_NEXT_BOARD		'b'
#define COMMAND_INCR_DELAY		'+'
#define COMMAND_DECR_DELAY		'-'
#define MAX_NUMBER_OF_SAMPLES	(1U << 12)
#define TIMEOUT_MS				(100)
#define DATA_FORMAT " \
	[ \
		{ \"name\" : \"TIMESTAMP\", \"type\" : \"U64\" }, \
		{ \"name\" : \"TRIGGER_ID\", \"type\" : \"U32\" }, \
		{ \"name\" : \"WAVEFORM\", \"type\" : \"U16\", \"dim\" : 2 }, \
		{ \"name\" : \"WAVEFORM_SIZE\", \"type\" : \"SIZE_T\", \"dim\" : 1 }, \
		{ \"name\" : \"EVENT_SIZE\", \"type\" : \"SIZE_T\" } \
	] \
"

// CFD settings
#define CFD_FRACTION			0.25
#define CFD_DELAY				32 // samples
#define CFD_ARMED_THR			-100.
#define CFD_PULSE_POLARITY		false // true if positive, false if negative

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

static int get_n_channels(uint64_t dev_handle, size_t* n_channels) {
	int ret;
	char value[256];
	ret = CAEN_FELib_GetValue(dev_handle, "/par/NumCh", value);
	if (ret != CAEN_FELib_Success) return ret;
	*n_channels = (size_t)value_to_ull(value);
	return ret;
}

static int get_sampling_rate(uint64_t dev_handle, double* sampling_rate) {
	int ret;
	char value[256];
	ret = CAEN_FELib_GetValue(dev_handle, "/par/ADC_SamplRate", value);
	if (ret != CAEN_FELib_Success) return ret;
	*sampling_rate = value_to_d(value);
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
	ret = CAEN_FELib_SetValue(dev_handle, par_name, "True");
	if (ret != CAEN_FELib_Success) return ret;

	const unsigned int record_length = MAX_NUMBER_OF_SAMPLES; // in samples
	snprintf(value, sizeof(value), "%u", record_length);
	ret = CAEN_FELib_SetValue(dev_handle, "/par/RecordLengthS", value);
	if (ret != CAEN_FELib_Success) return ret;

	const unsigned int pre_trigger = 100; // in samples
	snprintf(value, sizeof(value), "%u", pre_trigger);
	ret = CAEN_FELib_SetValue(dev_handle, "/par/PreTriggerS", value);
	if (ret != CAEN_FELib_Success) return ret;

	ret = CAEN_FELib_SetValue(dev_handle, "/par/AcqTriggerSource", "SwTrg | ITLA");
	if (ret != CAEN_FELib_Success) return ret;

	const double pulse_period = 100e3; // in nsec
	snprintf(value, sizeof(value), "%f", pulse_period);
	ret = CAEN_FELib_SetValue(dev_handle, "/par/TestPulsePeriod", value);
	if (ret != CAEN_FELib_Success) return ret;

	const unsigned int pulse_width = 128; // in nsec
	snprintf(value, sizeof(value), "%u", pulse_width);
	ret = CAEN_FELib_SetValue(dev_handle, "/par/TestPulseWidth", value);
	if (ret != CAEN_FELib_Success) return ret;

	const unsigned int pulse_low = 0; // in ADC counts
	snprintf(value, sizeof(value), "%u", pulse_low);
	ret = CAEN_FELib_SetValue(dev_handle, "/par/TestPulseLowLevel", value);
	if (ret != CAEN_FELib_Success) return ret;

	const unsigned int pulse_high = 10000; // in ADC counts
	snprintf(value, sizeof(value), "%u", pulse_high);
	ret = CAEN_FELib_SetValue(dev_handle, "/par/TestPulseHighLevel", value);
	if (ret != CAEN_FELib_Success) return ret;

	const double dc_offset = 50.; // in percent
	snprintf(value, sizeof(value), "%f", dc_offset);
	snprintf(par_name, sizeof(par_name), "/ch/0..%zu/par/DCOffset", n_channels - 1);
	ret = CAEN_FELib_SetValue(dev_handle, par_name, value);
	if (ret != CAEN_FELib_Success) return ret;

	snprintf(par_name, sizeof(par_name), "/ch/0..%zu/par/ITLConnect", n_channels - 1);
	ret = CAEN_FELib_SetValue(dev_handle, par_name, "ITLA");
	if (ret != CAEN_FELib_Success) return ret;

	const int trigger_thr = 9000; // in ADC counts
	snprintf(value, sizeof(value), "%d", trigger_thr);
	snprintf(par_name, sizeof(par_name), "/ch/0..%zu/par/TriggerThr", n_channels - 1);
	ret = CAEN_FELib_SetValue(dev_handle, par_name, value);
	if (ret != CAEN_FELib_Success) return ret;

	snprintf(par_name, sizeof(par_name), "/ch/0..%zu/par/TriggerThrMode", n_channels - 1);
	ret = CAEN_FELib_SetValue(dev_handle, par_name, "Absolute");
	if (ret != CAEN_FELib_Success) return ret;

	const unsigned int samples_over_threshold = 16; // in samples
	snprintf(value, sizeof(value), "%u", samples_over_threshold);
	snprintf(par_name, sizeof(par_name), "/ch/0..%zu/par/SamplesOverThreshold", n_channels - 1);
	ret = CAEN_FELib_SetValue(dev_handle, par_name, value);
	if (ret != CAEN_FELib_Success) return ret;

	snprintf(par_name, sizeof(par_name), "/ch/0..%zu/par/SelfTriggerEdge", n_channels - 1);
	ret = CAEN_FELib_SetValue(dev_handle, par_name, "Rise");
	if (ret != CAEN_FELib_Success) return ret;

	return ret;

}

static int get_run_delay(size_t board_id, size_t n_boards) {

	const bool first_board = (board_id == 0);
	const size_t board_id_from_last = n_boards - board_id - 1;

	// Run delay in clock cycles: 2 cycles per board
	int run_delay_clk = 2 * (int)board_id_from_last;

	// additional 4 cycles in the first board
	if (first_board)
		run_delay_clk += 4;

	const int run_delay_ns = run_delay_clk * 8; // clock period is 8 ns for all boards

	return run_delay_ns;

}

static int get_clock_out_delay(size_t board_id, size_t n_boards) {

	const bool first_board = (board_id == 0);
	const bool last_board = (board_id == n_boards - 1);

	const int clock_out_delay_ps = last_board ? 0 : (first_board ? -2148 : -3111);

	return clock_out_delay_ps;

}

static int configure_sync(uint64_t dev_handle, size_t board_id, size_t n_boards) {

	int ret;
	char run_delay_value[256];
	char clock_out_delay_value[256];

	const bool first_board = (board_id == 0);
	const bool last_board = (board_id == n_boards - 1);

	const int run_delay = get_run_delay(board_id, n_boards);
	snprintf(run_delay_value, sizeof(run_delay_value), "%d", run_delay);

	const int clock_out_delay = get_clock_out_delay(board_id, n_boards);
	snprintf(clock_out_delay_value, sizeof(clock_out_delay_value), "%d", clock_out_delay);

	ret = CAEN_FELib_SetValue(dev_handle, "/par/ClockSource", first_board ? "Internal" : "FPClkIn");
	if (ret != CAEN_FELib_Success) return ret;
	ret = CAEN_FELib_SetValue(dev_handle, "/par/SyncOutMode", last_board ? "Disabled" : (first_board ? "Run" : "SyncIn"));
	if (ret != CAEN_FELib_Success) return ret;
	ret = CAEN_FELib_SetValue(dev_handle, "/par/StartSource", first_board ? "SWcmd" : "EncodedClkIn");
	if (ret != CAEN_FELib_Success) return ret;
	ret = CAEN_FELib_SetValue(dev_handle, "/par/EnClockOutFP", last_board ? "False" : "True");
	if (ret != CAEN_FELib_Success) return ret;
	ret = CAEN_FELib_SetValue(dev_handle, "/par/RunDelay", run_delay_value);
	if (ret != CAEN_FELib_Success) return ret;
	ret = CAEN_FELib_SetValue(dev_handle, "/par/VolatileClockOutDelay", clock_out_delay_value);
	if (ret != CAEN_FELib_Success) return ret;
	ret = CAEN_FELib_SetValue(dev_handle, "/par/EnAutoDisarmAcq", "True");
	if (ret != CAEN_FELib_Success) return ret;

	// 62.5 MHz clock on TrgOut for delay measurement
	ret = CAEN_FELib_SetValue(dev_handle, "/par/TrgOutMode", "RefClk");
	if (ret != CAEN_FELib_Success) return ret;

	// TestPulse wave on DACOut for debug
	ret = CAEN_FELib_SetValue(dev_handle, "/par/DACOutMode", "Square");
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

/*
 * Allocated once per board, reused for each call
 * to CAEN_FELib_ReadData. Fields to be shared
 * with the data_thread will be copied to an instance
 * of struct processed_event before reading the
 * next event.
 */
struct event {
	// costants
	size_t board_id;
	size_t n_channels;
	double adc_sampling_period_ns;

	// native fields
	uint64_t timestamp;
	uint32_t trigger_id;
	size_t event_size;
	uint16_t** waveform;
	size_t* n_samples;
	size_t* n_allocated_samples;
};

/*
 * Almost same of struct event but containing only those
 * fields that we want to share with the data_thread.
 * It is allocated for each received event in the acq_thread
 * and freed in the data_thread once the information has
 * been processed.
 */
struct processed_event {
	// costants
	size_t board_id;
	size_t n_channels;
	double adc_sampling_period_ns;

	// event fields
	uint64_t timestamp;
	uint32_t trigger_id;
	size_t event_size;
	double* zero_crossing_ns;
};

struct shared_data {
	size_t n_boards;
	mtx_t* acq_mtx;
	cnd_t* acq_cnd;
	bool* acq_started;
	tlock_queue_t* evt_queue;
};

struct acq_data {
	uint64_t dev_handle;
	size_t board_id;
	mtx_t mtx;
	cnd_t cnd;
	bool ep_configured;
	struct shared_data shared_data;
};

static void print_stats(double t, size_t n_events, double rate) {
	printf("\x1b[1K\rTime (s): %.1f\tEvents: %zu\tReadout rate (MB/s): %f", t, n_events, rate);
	fflush(stdout);
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

static bool enqueue_processed_event(struct processed_event* evt, tlock_queue_t* evt_queue) {

	const int tlock_ret = tlock_push(evt_queue, evt);
	if (tlock_ret != TLOCK_OK) {
		fprintf(stderr, "tlock_push failed\n");
		free(evt);
		return false;
	}

	return true;

}

/*
 * ADD YOUR CODE HERE!
 *
 * Example function that extract some information from
 * the event generated by the digitizer.
 *
 * For example, here we implement a CFD to fill
 * evt->zero_crossing_ns for each channel. If not found,
 * the value is left set to nan("zc").
 */
static struct processed_event* generate_processed_event(const struct event* evt) {

	// Allocate event to be passed to data thread, free called there
	struct processed_event* processed_evt = malloc(sizeof(*processed_evt));
	if (processed_evt == NULL) {
		fprintf(stderr, "malloc failed");
		thrd_exit(EXIT_FAILURE);
	}

	processed_evt->zero_crossing_ns = malloc(evt->n_channels * sizeof(*processed_evt->zero_crossing_ns));
	if (processed_evt->zero_crossing_ns == NULL) {
		fprintf(stderr, "malloc failed");
		thrd_exit(EXIT_FAILURE);
	}

	// Copy native fields
	processed_evt->board_id = evt->board_id;
	processed_evt->n_channels = evt->n_channels;
	processed_evt->adc_sampling_period_ns = evt->adc_sampling_period_ns;
	processed_evt->timestamp = evt->timestamp;
	processed_evt->trigger_id = evt->trigger_id;
	processed_evt->event_size = evt->event_size;

	// Get maximum n_samples to allocate utility buffers once

	size_t max_n_samples = 0;

	for (size_t ch = 0; ch < evt->n_channels; ++ch) {

		// Initialize also zero_crossing on this loop
		processed_evt->zero_crossing_ns[ch] = nan("zc");

		if (evt->n_samples[ch] > max_n_samples)
			max_n_samples = evt->n_samples[ch];

	}

	if (max_n_samples == 0)
		return processed_evt;

	// Allocate buffers for smoothed waveform and discriminator signal

	double* const waveform_smoothed = malloc(max_n_samples * sizeof(*waveform_smoothed));
	if (waveform_smoothed == NULL) {
		fprintf(stderr, "malloc failed");
		thrd_exit(EXIT_FAILURE);
	}

	double* const discriminator = malloc(max_n_samples * sizeof(*discriminator));
	if (discriminator == NULL) {
		fprintf(stderr, "malloc failed");
		thrd_exit(EXIT_FAILURE);
	}

	// Calculate CFD for all channels

	for (size_t ch = 0; ch < evt->n_channels; ++ch) {

		const size_t n_samples = evt->n_samples[ch];
		if (n_samples == 0)
			continue;

		const uint16_t* waveform = evt->waveform[ch];

		// Calculate baseline and fill smoothed waveform

		const size_t n_baseline_samples = 16; // hardcoded
		assert(n_samples > n_baseline_samples);

		double baseline_acc = 0.;
		for (size_t j = 0; j < n_baseline_samples; ++j)
			baseline_acc += waveform[j];

		const double baseline = baseline_acc / n_baseline_samples;

		for (size_t i = 0; i < n_samples; ++i) {
			// todo add smooting
			waveform_smoothed[i] = waveform[i] - baseline;
		}

		// Calculate CFD

		// constants, hardcoded
		const double cfd_fraction = CFD_FRACTION;
		const size_t cfd_delay = CFD_DELAY; // samples
		const double cfr_armed_thr = CFD_ARMED_THR;
		const bool pulse_polarity_positive = CFD_PULSE_POLARITY;

		assert(cfd_fraction > 0.);
		assert(cfr_armed_thr < 0.);

		// discriminator is intended for negative pulses; if positive invert it
		const double pulse_sign = pulse_polarity_positive ? -1. : 1.;

		bool cfd_armed = false;

		for (size_t i = 0; i < n_samples; ++i) {

			const double waveform_attenuated = cfd_fraction * waveform_smoothed[i];
			const double waveform_delayed = (i < cfd_delay) ? 0. : waveform_smoothed[i - cfd_delay];

			discriminator[i] = pulse_sign * (waveform_attenuated - waveform_delayed);

			// arm cfd on a negative threshold
			if (!cfd_armed && (discriminator[i] < cfr_armed_thr))
				cfd_armed = true;

			// if armed, detect zero crossing
			if (cfd_armed && (discriminator[i] >= 0.)) {

				// consistency checks
				assert(i > 0); // cannot occour on first sample

				// zero crossing settings
				const size_t zc_i = i - 1;
				const double zc_negative = discriminator[i - 1];
				const double zc_positive = discriminator[i];

				// consistency checks
				assert(zc_negative < 0.); // discriminator was negative previous sample
				assert(zc_positive >= 0.); // implied by if-condition
				assert(zc_negative != zc_positive);

				// calculate fine zero crossing by linear interpolation
				const double zc_linear_interpolation = zc_negative / (zc_negative - zc_positive);
				const double zc_fine = zc_i + zc_linear_interpolation;

				// consistency checks
				assert(zc_linear_interpolation > 0.); // implied by previous assertions
				assert(zc_fine >= zc_i); // implied by previous assertions

				// set zero crossing to the event
				processed_evt->zero_crossing_ns[ch] = evt->adc_sampling_period_ns * zc_fine;

				// interrupt the search at the first zero crossing
				break;

			}

		}

	}

	free(discriminator);
	free(waveform_smoothed);

	return processed_evt;

}

static struct processed_event* generate_stop_event() {

	// Allocate event to be passed to data thread, free called there
	struct processed_event* processed_evt = malloc(sizeof(*processed_evt));
	if (processed_evt == NULL) {
		fprintf(stderr, "malloc failed");
		thrd_exit(EXIT_FAILURE);
	}

	processed_evt->zero_crossing_ns = NULL;
	processed_evt->event_size = 0; // fake event

	return processed_evt;

}

static void read_data_loop(uint64_t ep_handle, struct event* evt, tlock_queue_t* evt_queue) {

	for (;;) {

		const int ret = CAEN_FELib_ReadData(ep_handle, TIMEOUT_MS,
			&evt->timestamp,
			&evt->trigger_id,
			evt->waveform,
			evt->n_samples,
			&evt->event_size
		);
		switch (ret) {
		case CAEN_FELib_Success: {

			// extract custom information from the native event
			struct processed_event* processed_evt = generate_processed_event(evt);

			// pass event to data thread
			if (!enqueue_processed_event(processed_evt, evt_queue)) {
				fprintf(stderr, "enqueue_processed_event failed\n");
			}

			break;
		}
		case CAEN_FELib_Timeout:
			break;
		case CAEN_FELib_Stop:
			printf("\nStop received.\n");

			struct processed_event* stop_evt = generate_stop_event();

			// signal end of run with fake event to data thread
			if (!enqueue_processed_event(stop_evt, evt_queue)) {
				fprintf(stderr, "enqueue_processed_event failed\n");
			}

			return;
		default:
			print_last_error();
			break;
		}
	}
}

static struct event* allocate_event(size_t n_samples, size_t n_channels) {
	struct event* const evt = malloc(sizeof(*evt));
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
	free(evt->n_samples);
	free(evt->n_allocated_samples);
	for (size_t i = 0; i < evt->n_channels; ++i)
		free(evt->waveform[i]);
	free(evt);
}

static int acq_thread(void* p) {
	
	int ret;
	
	struct acq_data* data = (struct acq_data*)p;
	uint64_t ep_handle;

	size_t n_channels;
	ret = get_n_channels(data->dev_handle, &n_channels);
	if (ret != CAEN_FELib_Success) {
		print_last_error();
		return EXIT_FAILURE;
	}

	double sampling_rate; // in MHz
	ret = get_sampling_rate(data->dev_handle, &sampling_rate);
	if (ret != CAEN_FELib_Success) {
		print_last_error();
		return EXIT_FAILURE;
	}

	// reset
	ret = CAEN_FELib_SendCommand(data->dev_handle, "/cmd/reset");
	if (ret != CAEN_FELib_Success) {
		print_last_error();
		return EXIT_FAILURE;
	}

	// configure board
	ret = configure_digitizer(data->dev_handle, n_channels);
	if (ret != CAEN_FELib_Success) {
		print_last_error();
		return EXIT_FAILURE;
	}

	ret = configure_sync(data->dev_handle, data->board_id, data->shared_data.n_boards);
	if (ret != CAEN_FELib_Success) {
		print_last_error();
		return EXIT_FAILURE;
	}

	// configure endpoint
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
	struct event* evt = allocate_event(MAX_NUMBER_OF_SAMPLES, n_channels);

	// initialize event costants
	evt->board_id = data->board_id;
	evt->adc_sampling_period_ns = 1000. / sampling_rate;

	// arm acquisition
	ret = CAEN_FELib_SendCommand(data->dev_handle, "/cmd/armacquisition");
	if (ret != CAEN_FELib_Success) {
		print_last_error();
		return EXIT_FAILURE;
	}

	// signal main thread
	mtx_lock(&data->mtx);
	data->ep_configured = true;
	mtx_unlock(&data->mtx);
	cnd_signal(&data->cnd);

	// wait main thread
	mtx_lock(data->shared_data.acq_mtx);
	while (!*data->shared_data.acq_started)
		cnd_wait(data->shared_data.acq_cnd, data->shared_data.acq_mtx);
	mtx_unlock(data->shared_data.acq_mtx);

	// acquisition loop
	read_data_loop(ep_handle, evt, data->shared_data.evt_queue);

	free_event(evt);

	return EXIT_SUCCESS;
}

// inspired to to C++ std::vector
struct evt_list {
	struct processed_event** data;
	size_t size;
	size_t capacity;
};

static void add_evt_to_list(struct evt_list* evt_list, struct processed_event* evt) {

	// local copy of evt_list to reduce dereferences on this function. evt_list is updated on exit.
	struct evt_list local_evt_list = *evt_list;

	// adjust capacity if required
	if (local_evt_list.capacity <= local_evt_list.size) {
		assert(local_evt_list.capacity != 0);
		const size_t new_capacity = local_evt_list.capacity * 2; // double capacity
		struct processed_event** new_data = realloc(local_evt_list.data, sizeof(*local_evt_list.data) * new_capacity);
		if (new_data == NULL) {
			fprintf(stderr, "realloc failed");
			thrd_exit(EXIT_FAILURE);
		}
		local_evt_list.data = new_data;
		local_evt_list.capacity = new_capacity;
	}
	assert(local_evt_list.capacity > local_evt_list.size);

	// get evt timestamp
	const uint64_t evt_timestamp = evt->timestamp;

	// add new event in sorted list (current elements are already sorted, qsort would be overkilling)
	size_t evt_pos = local_evt_list.size;
	for (; evt_pos != 0; --evt_pos)
		if (local_evt_list.data[evt_pos - 1]->timestamp < evt_timestamp)
			break;
	const size_t n_evt_moved = local_evt_list.size - evt_pos;
	memmove(local_evt_list.data + evt_pos + 1, local_evt_list.data + evt_pos, n_evt_moved * sizeof(*local_evt_list.data));
	local_evt_list.data[evt_pos] = evt;
	++local_evt_list.size;

	// update evt_list
	*evt_list = local_evt_list;

}

static void process_evt_list(struct evt_list* evt_list) {

	const uint64_t timestamp_window = 125000000; // 1 s

	// local copy of evt_list to reduce dereferences on this function. evt_list is updated on exit.
	struct evt_list local_evt_list = *evt_list;

	// nothing to do if there is only an event
	if (local_evt_list.size == 1)
		return;

	/*
	 * ADD YOUR CODE HERE!
	 *
	 * Process the event list here!
	 */

	// get most recent timestamp
	const uint64_t last_timestamp = local_evt_list.data[local_evt_list.size - 1]->timestamp;

	// free data outside current window
	size_t i = 0; // counter of removed events
	for (; i < local_evt_list.size; ++i) {
		if (last_timestamp - local_evt_list.data[i]->timestamp > timestamp_window) {

			/*
			 * ADD YOUR CODE HERE!
			 *
			 * Process data just before removing old events
			 *
			 * Here we do not search for coincidences between board,
			 * we just fill an histogram with dt between channels of a specific board.
			 */

			free(local_evt_list.data[i]->zero_crossing_ns);
			free(local_evt_list.data[i]);

		} else {
			break; // events are sorted: next events are more recents
		}
	}

	// remove removed items and update evt_list
	if (i != 0) {
		local_evt_list.size -= i;
		memmove(local_evt_list.data, local_evt_list.data + i, local_evt_list.size * sizeof(*local_evt_list.data));
		*evt_list = local_evt_list;
	}

}

static int data_thread(void* p) {

	struct shared_data* data = (struct shared_data*)p;

	size_t missing_stop_events = data->n_boards;

	struct counters total;
	struct counters interval;

	counters_reset(&total, time(NULL));
	counters_reset(&interval, total.t_begin);

	const size_t initial_capacity = 1;
	struct evt_list evt_list = {
		.data = malloc(sizeof(*evt_list.data) * initial_capacity),
		.size = 0,
		.capacity = initial_capacity
	};

	if (evt_list.data == NULL) {
		fprintf(stderr, "malloc failed");
		return EXIT_FAILURE;
	}

	// wait main thread
	mtx_lock(data->acq_mtx);
	while (!*data->acq_started)
		cnd_wait(data->acq_cnd, data->acq_mtx);
	mtx_unlock(data->acq_mtx);

	while (missing_stop_events != 0) {

		const time_t current_time = time(NULL);
		const double dt = counters_dt(&interval, current_time);
		if (dt >= 1.) {
			// print stats
			print_stats(counters_dt(&total, current_time), total.n_events, counters_rate(&interval, current_time));
			counters_reset(&interval, current_time);
		}

		struct processed_event* evt = tlock_pop(data->evt_queue);
		if (evt == NULL) {
			// wait 100 ms
			thrd_sleep(&(struct timespec){.tv_nsec=100000000}, NULL);
			continue;
		}

		if (evt->event_size == 0) {
			// fake event to signal end of run
			--missing_stop_events;
			free(evt);
			continue;
		}

		counters_increment(&total, evt->event_size);
		counters_increment(&interval, evt->event_size);

		add_evt_to_list(&evt_list, evt);

		process_evt_list(&evt_list);

	}

	// free remaining events
	for (size_t i = 0; i < evt_list.size; ++i)
		free(evt_list.data[i]);
	free(evt_list.data);

	return EXIT_SUCCESS;

}

static int increment_clock_out_delay(uint64_t dev_handle, int n_steps) {

	int ret;
	char value[256];

	const char par_name[] = "/par/VolatileClockOutDelay";
	const char incr_attribute_name[] = "/par/VolatileClockOutDelay/increment";

	// get increment from parameter attribute
	ret = CAEN_FELib_GetValue(dev_handle, incr_attribute_name, value);
	if (ret != CAEN_FELib_Success) return ret;
	const double incr = value_to_d(value);
	// get current value
	ret = CAEN_FELib_GetValue(dev_handle, par_name, value);
	if (ret != CAEN_FELib_Success) return ret;
	const double current_clock_out = value_to_d(value);
	// set new value
	const double new_clock_out = current_clock_out + (n_steps * incr);
	snprintf(value, sizeof(value), "%f", new_clock_out);
	ret = CAEN_FELib_SetValue(dev_handle, par_name, value);
	if (ret != CAEN_FELib_Success) return ret;

	return CAEN_FELib_Success;

}

int main(int argc, char* argv[]) {

	int ret;

	printf("##########################################\n");
	printf("\tCAEN firmware Sync demo\n");
	printf("##########################################\n");

	if (argc == 1) {
		fputs("invalid arguments", stderr);
		return EXIT_FAILURE;
	}

	const size_t n_boards = argc - 1;

	struct acq_data* const board_data = malloc(sizeof(*board_data) * n_boards);
	if (board_data == NULL) {
		fprintf(stderr, "malloc failed\n");
		return EXIT_FAILURE;
	}

	thrd_t* const thrds = malloc(sizeof(*thrds) * n_boards);
	if (thrds == NULL) {
		fprintf(stderr, "malloc failed\n");
		return EXIT_FAILURE;
	}

	// initialize variabled shared by threads
	bool acq_started = false;
	mtx_t acq_mtx;
	cnd_t acq_cnd;
	mtx_init(&acq_mtx, mtx_plain);
	cnd_init(&acq_cnd);
	tlock_queue_t* const evt_queue = tlock_init();
	if (evt_queue == NULL) {
		fprintf(stderr, "tlock_init failed\n");
		return EXIT_FAILURE;
	}

	struct shared_data shared_data = {
		.n_boards = n_boards,
		.acq_mtx = &acq_mtx,
		.acq_cnd = &acq_cnd,
		.acq_started = &acq_started,
		.evt_queue = evt_queue
	};

	// open devices
	for (size_t i = 0; i < n_boards; ++i) {

		struct acq_data* data = &board_data[i];

		// CAEN_FELib_Open is not thread safe
		const char* path = argv[i + 1];
		printf("device path: %s\n", path);

		ret = CAEN_FELib_Open(path, &data->dev_handle);
		if (ret != CAEN_FELib_Success) {
			print_last_error();
			return EXIT_FAILURE;
		}

		// initialize other fields
		data->board_id = i;
		mtx_init(&data->mtx, mtx_plain);
		cnd_init(&data->cnd);
		data->ep_configured = false;

		// copy shared_data
		data->shared_data = shared_data;

		ret = print_digitizer_details(data->dev_handle);
		if (ret != CAEN_FELib_Success) {
			print_last_error();
			return EXIT_FAILURE;
		}

		ret = thrd_create(thrds + i, acq_thread, data);
		if (ret != thrd_success) {
			fprintf(stderr, "thrd_create failed");
			return EXIT_FAILURE;
		}
	}

	printf("Configuring...\t");

	// wait configuration on acquisition thread
	for (size_t i = 0; i < n_boards; ++i) {
		struct acq_data* data = &board_data[i];
		mtx_lock(&data->mtx);
		while (!data->ep_configured)
			cnd_wait(&data->cnd, &data->mtx);
		mtx_unlock(&data->mtx);
	}

	printf("done.\n");

	printf("Starting...\t");

	// launch data collection thread
	thrd_t data_thrd;
	ret = thrd_create(&data_thrd, &data_thread, &shared_data);
	if (ret != thrd_success) {
		fprintf(stderr, "thrd_create failed");
		return EXIT_FAILURE;
	}

	// send software start on first board only
	ret = CAEN_FELib_SendCommand(board_data[0].dev_handle, "/cmd/swstartacquisition");
	if (ret != CAEN_FELib_Success) {
		print_last_error();
		return EXIT_FAILURE;
	}

	printf("done.\n");

	// notify start to acquisition thread
	mtx_lock(&acq_mtx);
	acq_started = true;
	mtx_unlock(&acq_mtx);
	cnd_broadcast(&acq_cnd);

	printf("##########################################\n");
	printf("Commands supported:\n");
	printf("\t[%c]\tselect next board\n", COMMAND_NEXT_BOARD);
	printf("\t[%c]\tincrement clock out delay of current board by minimum step\n", COMMAND_INCR_DELAY);
	printf("\t[%c]\tdecrement clock out delay of current board by minimum step\n", COMMAND_DECR_DELAY);
	printf("\t[%c]\tsend manual trigger to current board\n", COMMAND_TRIGGER);
	printf("\t[%c]\tstop acquisition\n", COMMAND_STOP);
	printf("##########################################\n");

	bool do_quit = false;
	size_t current_board = 0;

	do {
		const int c = _getch();
		switch (c) {
		case COMMAND_TRIGGER: {
			ret = CAEN_FELib_SendCommand(board_data[current_board].dev_handle, "/cmd/sendswtrigger");
			if (ret != CAEN_FELib_Success)
				print_last_error();
			break;
		}
		case COMMAND_STOP: {
			do_quit = true;
			break;
		}
		case COMMAND_NEXT_BOARD: {
			if (++current_board == n_boards)
				current_board = 0;
			break;
		}
		case COMMAND_INCR_DELAY: {
			ret = increment_clock_out_delay(board_data[current_board].dev_handle, 1);
			if (ret != CAEN_FELib_Success)
				print_last_error();
			break;
		}
		case COMMAND_DECR_DELAY: {
			ret = increment_clock_out_delay(board_data[current_board].dev_handle, -1);
			if (ret != CAEN_FELib_Success)
				print_last_error();
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

	ret = CAEN_FELib_SendCommand(board_data[0].dev_handle, "/cmd/swstopacquisition");
	if (ret != CAEN_FELib_Success) {
		print_last_error();
		return EXIT_FAILURE;
	}

	printf("done.\n");

	// wait the end of the acquisition
	// that is going to finish just after the last event
	for (size_t i = 0; i < n_boards; ++i) {
		struct acq_data* data = &board_data[i];
		int thrd_ret;
		ret = thrd_join(thrds[i], &thrd_ret);
		if (ret != thrd_success || thrd_ret != EXIT_SUCCESS) {
			fprintf(stderr, "thrd_join error.\tret %d\tthrd_ret %d\n", ret, thrd_ret);
			return EXIT_FAILURE;
		}
		mtx_destroy(&data->mtx);
		cnd_destroy(&data->cnd);

		ret = CAEN_FELib_Close(data->dev_handle);
		if (ret != CAEN_FELib_Success) {
			print_last_error();
			return EXIT_FAILURE;
		}
	}

	int data_thrd_ret;

	ret = thrd_join(data_thrd, &data_thrd_ret);
	if (ret != thrd_success || data_thrd_ret != EXIT_SUCCESS) {
		fprintf(stderr, "thrd_join error.\tret %d\tdata_thrd_ret %d\n", ret, data_thrd_ret);
		return EXIT_FAILURE;
	}

	mtx_destroy(&acq_mtx);
	cnd_destroy(&acq_cnd);
	tlock_free(evt_queue);

	printf("\nBye!\n");

	return EXIT_SUCCESS;
}
