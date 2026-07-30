#include "read_crossbar.h"

#include <ctype.h>
#include <errno.h> //maybe I could use this
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int run_ngspice(const char *crossbar_path)
{
	if (crossbar_path == NULL) {
		fprintf(
		    stderr,
		    "One of the file paths returned null when running ngspice");
		return -1;
	}

	char command[4096];
	int written =
	    snprintf(command, sizeof(command),
		     "ngspice -b \"%s\" > /dev/null 2>&1", crossbar_path);

	if (written < 0) {
		fprintf(stderr, "Failed to write spice command");
		return -1;
	}

	int status = system(command);
	if (status < 0) {
		fprintf(stderr, "Failed to run ngspice command");
	}

	return 0;
}

int read_crossbar(const char *data_path, size_t num_outputs,
		  Crossbar_Output_Matrix *result)
{

	FILE *file = fopen(data_path, "r");
	if (!file) {
		fprintf(stderr, "Faile to open data file");
		return -1;
	}

	result->num_samples = 0;
	result->num_outputs = num_outputs;
	result->time = NULL;
	result->voltages = NULL;

	size_t capacity = 100000;

	result->time = malloc(capacity * sizeof(*result->time));
	result->voltages =
	    malloc(capacity * num_outputs * sizeof(*result->voltages));

	if (result->time == NULL || result->voltages == NULL) {
		fprintf(stderr, "Failed to allocate memory for results");
		fclose(file);
		free_crossbar_output_matrix(result);
		return -1;
	}

	size_t expected_fields = num_outputs * 2;
	double *fields = malloc(expected_fields * sizeof(*fields));
	if (fields == NULL) {
		fprintf(stderr, "failed to allocate memory for fields");
		fclose(file);
		free_crossbar_output_matrix(result);
		return -1;
	}

	char line[16384];
	// size_t line_number = 0;

	while (fgets(line, sizeof(line), file) != NULL) {
		/* Raises capacity if needed, not working rn tho */
		// if (result->num_samples == capacity) {
		//     capacity *= 2;
		//
		//     result->time = realloc(result->time, capacity *
		//     sizeof(double)); result->voltages =
		//     realloc(result->voltages, capacity * sizeof(double));
		//
		//     if (result->time == NULL || result->voltages == NULL) {
		//         fprintf(stderr, "Failed to reallocate memory");
		//         fclose(file);
		//         return -1;
		//     }
		// }

		char *position = line;
		double sample_time = 0.0;

		// each loop reads one time-voltage pair, saves it
		// then moves to next loop (next time-voltage pair)
		for (size_t output = 0; output < num_outputs; output++) {
			double time;
			double voltage;
			int char_count;

			if (sscanf(position, "%lf %lf %n", &time, &voltage,
				   &char_count) != 2) {
				fprintf(stderr, "invalide data line");
				fclose(file);
				return -1;
			}

			if (output == 0) {
				sample_time = time;
			}

			result->voltages[result->num_samples * num_outputs +
					 output] = voltage;
			position += char_count;
		}
		result->time[result->num_samples] = sample_time;
		result->num_samples++;
	}
	fclose(file);
	return 0;
}

int convert_output_to_software(size_t num_neurons, size_t num_outputs,
			       size_t num_timesteps, const double *voltages,
			       const double *resistances,
			       double load_resistance,
			       const conductance_mapping *mapping,
			       const double *row_voltages,
			       double spike_amplitude, double *decoded_outputs)
{
	// Using differential pair mapping
	size_t num_physical_columns;

	if (!voltages || !resistances || !mapping || !decoded_outputs)
		return -1;

	if (load_resistance <= 0.0 || mapping->alpha == 0.0 ||
	    spike_amplitude == 0.0)
		return -1;

	num_physical_columns = num_outputs * 2;

	for (size_t timestep = 0; timestep < num_timesteps; timestep++) {
		for (size_t output = 0; output < num_outputs; output++) {
			size_t positive_column;
			size_t negative_column;
			size_t positive_voltage_index;
			size_t negative_voltage_index;
			size_t output_index;
			double positive_conductance_sum = 0.0;
			double negative_conductance_sum = 0.0;
			double positive_voltage;
			double negative_voltage;
			double positive_load_current;
			double negative_load_current;
			double positive_source_current;
			double negative_source_current;

			positive_column = 2 * output;
			negative_column = positive_column + 1;

			for (size_t neuron = 0; neuron < num_neurons;
			     neuron++) {
				size_t positive_resistance_index;
				size_t negative_resistance_index;

				positive_resistance_index =
				    neuron * num_physical_columns +
				    positive_column;

				negative_resistance_index =
				    neuron * num_physical_columns +
				    negative_column;

				positive_conductance_sum +=
				    1.0 /
				    resistances[positive_resistance_index];

				negative_conductance_sum +=
				    1.0 /
				    resistances[negative_resistance_index];
			}

			positive_voltage_index =
			    timestep * num_physical_columns + positive_column;

			negative_voltage_index =
			    timestep * num_physical_columns + negative_column;

			positive_voltage = voltages[positive_voltage_index];

			negative_voltage = voltages[negative_voltage_index];

			positive_load_current =
			    positive_voltage / load_resistance;

			negative_load_current =
			    negative_voltage / load_resistance;

			positive_source_current =
			    positive_load_current +
			    positive_voltage * positive_conductance_sum;

			negative_source_current =
			    negative_load_current +
			    negative_voltage * negative_conductance_sum;

			output_index = timestep * num_outputs + output;

			decoded_outputs[output_index] =
			    (positive_source_current -
			     negative_source_current) /
			    (mapping->alpha * spike_amplitude);
		}
	}

	return 0;
}

void free_crossbar_output_matrix(Crossbar_Output_Matrix *result)
{
	if (result == NULL) {
		return;
	}

	free(result->time);
	free(result->voltages);

	result->num_samples = 0;
	result->num_outputs = 0;
	result->time = NULL;
	result->voltages = NULL;
}
