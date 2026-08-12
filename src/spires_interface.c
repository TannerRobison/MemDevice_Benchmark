#include "spires_interface.h"
#include <spires.h>

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int collect_reservoir_states(spires_reservoir *reservoir,
			     const double *input_series, size_t series_length,
			     Reservoir_State_Matrix *result)
{
	// error checking
	// RIP

	// clear the result first
	result->num_samples = 0;
	result->num_features = 0;
	result->states = NULL;

	const size_t num_inputs = spires_num_inputs(reservoir);

	const size_t num_neurons = spires_num_neurons(reservoir);

	if (series_length > SIZE_MAX / num_neurons ||
	    series_length * num_neurons > SIZE_MAX / sizeof(double)) {
		fprintf(stderr, "matrix size overloaded!!");
		return -1;
	}

	double *states = malloc(num_neurons * series_length * sizeof(*states));
	if (!states) {
		fprintf(stderr, "failed to allocate memory for states");
		return -1;
	}

	spires_status status = spires_reservoir_reset(reservoir);
	if (status != SPIRES_OK) {
		fprintf(stderr, "reservoir reset error");
		free(states);
		return -1;
	}

	// build state_matrix
	for (size_t i = 0; i < series_length; i++) {
		const double *current_input = &input_series[i * num_inputs];
		status = spires_step(reservoir, current_input);
		if (status != SPIRES_OK) {
			free(states);
			return -1;
		}

		double *current_state = &states[i * num_neurons];
		status = spires_read_reservoir_state(reservoir, current_state);
		if (status != SPIRES_OK) {
			free(states);
			return -1;
		}
	}

	result->num_samples = series_length;
	result->num_features = num_neurons;
	result->states = states;

	return 0;
}

int convert_weights_to_resistances(const spires_reservoir *reservoir,
				   size_t num_neurons, size_t num_outputs,
				   double r_on, double r_off,
				   double **resistances_out,
				   conductance_mapping *mapping)
{
	double max_abs_weight = 0.0;

	*resistances_out = NULL;
	size_t weight_count = num_neurons * num_outputs;
	size_t num_physical_columns = num_outputs * 2;

	double *readout = malloc(num_neurons * num_outputs * sizeof(double));

	spires_read_readout(reservoir, readout);

	double *resistances =
	    malloc(num_neurons * num_physical_columns * sizeof(double));
	if (resistances == NULL) {
		fprintf(stderr, "Failed to allocated crossbar resistances");
		free(readout);
		return -1;
	}

	// get the max weight
	for (size_t i = 0; i < weight_count; i++) {
		if (!isfinite(readout[i])) {
			fprintf(stderr,
				"invalid readout weight at index %zu: %g\n", i,
				readout[i]);
			free(readout);
			free(resistances);
			return -1;
		}

		double abs_weight = fabs(readout[i]);
		if (abs_weight > max_abs_weight) {
			max_abs_weight = abs_weight;
		}
	}

	mapping->g_min = 1.0 / r_off;
	mapping->g_max = 1.0 / r_on;
	mapping->max_abs_weight = max_abs_weight;

	// calculate the alpha scaling parameter to normalize the weights
	mapping->alpha = (mapping->g_max - mapping->g_min) / max_abs_weight;

	// differential pair mapping
	for (size_t neuron = 0; neuron < num_neurons; neuron++) {
		for (size_t output = 0; output < num_outputs; output++) {
			size_t weight_index = output * num_neurons + neuron;
			size_t positive_column = 2 * output;
			size_t negative_column = positive_column + 1;
			double positive_conductance;
			double negative_conductance;

			size_t positive_index =
			    neuron * num_physical_columns + positive_column;
			size_t negative_index =
			    neuron * num_physical_columns + negative_column;

			double weight = readout[weight_index];
			if (weight >= 0.0) {
				positive_conductance =
				    mapping->g_min + mapping->alpha * weight;
				negative_conductance = mapping->g_min;
			}
			if (weight < 0.0) {
				positive_conductance = mapping->g_min;
				negative_conductance =
				    mapping->g_min + mapping->alpha * (-weight);
			}

			resistances[positive_index] =
			    1.0 / positive_conductance;
			resistances[negative_index] =
			    1.0 / negative_conductance;
		}
	}
	// printf("max absolute weight = %.12e\n", mapping->max_abs_weight);
	// printf("alpha = %.12e\n", mapping->alpha);
	// printf("physical crossbar dimensions: %zu x %zu\n", num_neurons,
	//        num_physical_columns);

	*resistances_out = resistances;
	free(readout);

	return 0;
}

int train_reservoir(spires_reservoir *reservoir, double *input_series,
		    double *target_series, size_t series_length, double lambda)
{
	spires_status status =
	    spires_train_ridge(reservoir, (double *)input_series,
			       (double *)target_series, series_length, lambda);

	if (status != SPIRES_OK) {
		fprintf(stderr, "Spires ridge training failed");
		return -1;
	}

	return 0;
}

void free_reservoir_state_matrix(Reservoir_State_Matrix *matrix)
{
	if (!matrix) {
		return;
	}

	free(matrix->states);

	matrix->states = NULL;
	matrix->num_samples = 0;
	matrix->num_features = 0;
}
