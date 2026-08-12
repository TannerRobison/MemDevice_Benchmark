#include "benchmark.h"
#include "crossbar_generator.h"
#include "read_crossbar.h"
#include "spires_interface.h"

#include <math.h>
#include <plplot/plplot.h>
#include <spires.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SPIKE_THRESHOLD 0.1
#define SPIKE_AMPLITUDE 0.1

int run_benchmark(const spires_reservoir_config *config,
		  spires_reservoir *reservoir,
		  Reservoir_State_Matrix *state_matrix, const char *model_path,
		  const char *subcircuit_name, double *predictions_out)
{
	// copy readout weights and convert to conductances
	double *initial_resistances = NULL;
	conductance_mapping mapping;

	if (convert_weights_to_resistances(
		reservoir, config->num_neurons, config->num_outputs, 1000.0,
		100000.0, &initial_resistances, &mapping) != 0) {
		return -1;
	}

	double *row_voltages =
	    malloc(state_matrix->num_features * state_matrix->num_samples *
		   sizeof(double));

	if (row_voltages == NULL) {
		fprintf(stderr, "Failed to allocate spikes voltages");
		free(initial_resistances);
		free_reservoir_state_matrix(state_matrix);
		spires_reservoir_destroy(reservoir);
		return -1;
	}

	for (size_t sample = 0; sample < state_matrix->num_samples; sample++) {
		for (size_t neuron = 0; neuron < state_matrix->num_features;
		     neuron++) {
			size_t index =
			    sample * state_matrix->num_features + neuron;

			row_voltages[index] =
			    SPIKE_AMPLITUDE * state_matrix->states[index];
		}
	}

	const Crossbar_Config crossbar_config = {
	    .rows = state_matrix->num_features,
	    .columns = config->num_outputs * 2,
	    .input_series = row_voltages,
	    .num_samples = state_matrix->num_samples,
	    .initial_resistance = initial_resistances,
	    .load_resistance = 50.0,
	    .model_path = model_path,
	    .subcircuit_name = subcircuit_name,
	    .time_step = 1e-6,
	    .stop_time = state_matrix->num_samples * 1e-6,
	    .print_state_nodes = 0}; // state nodes is not acutally implemented

	if (generate_crossbar("output/crossbar.cir", &crossbar_config) < 0) {
		fprintf(stderr, "failed to create crossbar config");
		free(initial_resistances);
		free_reservoir_state_matrix(state_matrix);
		spires_reservoir_destroy(reservoir);
		return -1;
	}
	printf("Generated crossbar!!");

	// call ngspice for crossbar
	if (run_ngspice("output/crossbar.cir") < 0) {
		fprintf(stderr, "Failed to run_ngspice");
		free(initial_resistances);
		free_reservoir_state_matrix(state_matrix);
		spires_reservoir_destroy(reservoir);
		return -1;
	}
	printf("ran ngspice!!");

	// crossbar parameters needed for reading
	Crossbar_Output_Matrix crossbar_output = {
	    .num_samples = state_matrix->num_samples,
	    .num_outputs = config->num_outputs * 2,
	    .time = NULL,
	    .voltages = NULL};

	if (read_crossbar("output/crossbar_output.dat", config->num_outputs * 2,
			  &crossbar_output) < 0) {
		fprintf(stderr, "Failed to read crossbar output file");
		free(initial_resistances);
		free_reservoir_state_matrix(state_matrix);
		spires_reservoir_destroy(reservoir);
		free_crossbar_output_matrix(&crossbar_output);
		return -1;
	}
	printf("read the crossbar outputs!!\n");

	if (convert_output_to_software(
		config->num_neurons, config->num_outputs,
		state_matrix->num_samples, crossbar_output.voltages,
		initial_resistances, crossbar_config.load_resistance, &mapping,
		row_voltages, SPIKE_AMPLITUDE, predictions_out) < 0) {
		fprintf(stderr,
			"Failed to convert crossbar outputs back to software");
		return -1;
	}

	printf("YAY IT WORKED!!! Cleaning up :)\n");
	free(initial_resistances);
	free(row_voltages);
	free_crossbar_output_matrix(&crossbar_output);

	return 0;
}

double calculate_MSE(const double *expected, const double *predicted,
		     const size_t num_steps, const size_t num_outputs)
{
	double aggregate = 0.0;
	for (size_t output = 0; output < num_outputs; output++) {
		for (size_t timestep = 0; timestep < num_steps; timestep++) {
			double error =
			    expected[timestep * num_outputs + output] -
			    predicted[timestep * num_outputs + output];
			double squared = error * error;
			aggregate += squared;
		}
	}

	double full_mse = 100 * aggregate / (num_steps * num_outputs);
	return full_mse;
}

int plot_raster(const Reservoir_State_Matrix *matrix, size_t neurons_to_plot,
		double spike_threshold)
{
	if (!matrix || !matrix->states || matrix->num_samples == 0) {
		return -1;
	}

	if (neurons_to_plot > matrix->num_features) {
		neurons_to_plot = matrix->num_features;
	}

	// count spikes
	size_t spike_count = 0;
	for (size_t t = 0; t < matrix->num_samples; t++) {
		for (size_t n = 0; n < neurons_to_plot; n++) {
			double value =
			    matrix->states[t * matrix->num_features + n];
			if (value > spike_threshold) {
				spike_count++;
			}
		}
	}

	if (spike_count == 0) {
		fprintf(stderr, "No spikes found above threshold %.3f\n",
			spike_threshold);
		return -1;
	}

	PLFLT *x = malloc(spike_count * sizeof(*x));
	PLFLT *y = malloc(spike_count * sizeof(*y));
	if (!x || !y) {
		free(x);
		free(y);
		return -1;
	}

	// fill spike coordinates
	size_t k = 0;
	for (size_t t = 0; t < matrix->num_samples; t++) {
		for (size_t n = 0; n < neurons_to_plot; n++) {
			double value =
			    matrix->states[t * matrix->num_features + n];
			if (value > spike_threshold) {
				x[k] = (PLFLT)t;
				y[k] = (PLFLT)n;
				k++;
			}
		}
	}

	plsdev("svg");
	plsfnam("output/reservoir_raster.svg");

	plsetopt("geometry", "1600x1200");
	plscolbg(255, 255, 255);

	plinit();

	plscol0(1, 40, 40, 40); // gray axis
	plscol0(2, 0, 0, 0);

	plcol0(1);
	plwidth(1.0);

	plenv(0.0, (PLFLT)(matrix->num_samples - 1), 0.0,
	      (PLFLT)(neurons_to_plot - 1), 0, 0);

	pllab("Timestep", "Neuron index", "SPIRES Reservoir Raster Plot");

	plcol0(2);
	plwidth(1.0);

	for (size_t i = 0; i < spike_count; i++) {
		PLFLT xline[2] = {x[i], x[i]};
		PLFLT yline[2] = {y[i] - 0.35, y[i] + 0.35};

		plline(2, xline, yline);
	}

	plend();

	free(x);
	free(y);
	return 0;
}

int plot_reservoir_predictions(const double *expected, const double *predicted,
			       size_t num_samples, size_t num_outputs,
			       size_t output_to_plot, const char *model_path)
{
	PLFLT *x;
	PLFLT *y_expected;
	PLFLT *y_predicted;
	PLFLT y_min;
	PLFLT y_max;

	if (!expected || !predicted || num_samples == 0 ||
	    output_to_plot >= num_outputs)
		return -1;

	x = malloc(num_samples * sizeof(*x));
	y_expected = malloc(num_samples * sizeof(*y_expected));
	y_predicted = malloc(num_samples * sizeof(*y_predicted));

	if (!x || !y_expected || !y_predicted) {
		free(x);
		free(y_expected);
		free(y_predicted);
		return -1;
	}

	y_min = (PLFLT)expected[output_to_plot];
	y_max = y_min;

	for (size_t sample = 0; sample < num_samples; sample++) {
		size_t index;

		index = sample * num_outputs + output_to_plot;

		x[sample] = (PLFLT)sample;
		y_expected[sample] = (PLFLT)expected[index];
		y_predicted[sample] = (PLFLT)predicted[index];

		if (y_expected[sample] < y_min)
			y_min = y_expected[sample];

		if (y_expected[sample] > y_max)
			y_max = y_expected[sample];

		if (y_predicted[sample] < y_min)
			y_min = y_predicted[sample];

		if (y_predicted[sample] > y_max)
			y_max = y_predicted[sample];
	}

	{
		PLFLT margin;

		margin = (y_max - y_min) * 0.1;

		if (margin == 0.0)
			margin = 1.0;

		y_min -= margin;
		y_max += margin;
	}

	plsdev("svg");

	// parses real model name
	// written by chatGPT
	char filename[256];
	char model_name[128];
	const char *base;
	const char *dot;
	size_t len;

	base = strrchr(model_path, '/');
	base = base ? base + 1 : model_path;

	dot = strrchr(base, '.');
	len = dot ? (size_t)(dot - base) : strlen(base);

	if (len >= sizeof(model_name))
		len = sizeof(model_name) - 1;

	memcpy(model_name, base, len);
	model_name[len] = '\0';

	if (snprintf(filename, sizeof(filename),
		     "output/reservoir_prediction_%s.svg",
		     model_name) >= (int)sizeof(filename)) {
		fprintf(stderr, "Output filename is too long\n");
		return -1;
	}

	plsfnam(filename);

	plsetopt("geometry", "1600x1000");

	plscolbg(255, 255, 255);
	plinit();

	plscol0(1, 0, 0, 0);
	plscol0(2, 30, 90, 200);
	plscol0(3, 200, 50, 50);

	plcol0(1);
	plwidth(1.0);

	plenv(0.0, (PLFLT)(num_samples - 1), y_min, y_max, 0, 0);

	pllab("Timestep", "Output",
	      "Expected vs SPICE Crossbar readout Prediction");

	plcol0(2);
	plwidth(2.0);
	plline((PLINT)num_samples, x, y_expected);

	plcol0(3);
	plwidth(2.0);
	plline((PLINT)num_samples, x, y_predicted);

	plcol0(1);
	plcol0(2);
	plptex((PLFLT)(num_samples * 0.75), y_max - 0.08 * (y_max - y_min), 1.0,
	       0.0, 0.0, "Expected");

	plcol0(3);
	plptex((PLFLT)(num_samples * 0.75), y_max - 0.16 * (y_max - y_min), 1.0,
	       0.0, 0.0, "Predicted");

	plend();

	free(x);
	free(y_expected);
	free(y_predicted);

	return 0;
}
