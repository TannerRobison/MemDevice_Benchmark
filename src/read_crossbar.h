#ifndef READ_CROSSBAR_H
#define READ_CROSSBAR_H

#include "spires_interface.h"
#include <stddef.h>

typedef struct {
	size_t num_samples; // number of time-steps
	size_t num_outputs; // number of columns

	double *time;

	// stored in row major order
	// voltages[sameple * num_outputs + output]
	double *voltages;
} Crossbar_Output_Matrix;

int run_ngspice(const char *crossbar_path);

int read_crossbar(const char *data_path, size_t num_outputs,
		  Crossbar_Output_Matrix *result);

int convert_output_to_software(size_t num_neurons, size_t num_outputs,
			       size_t num_timesteps, const double *voltages,
			       const double *resistances,
			       double load_resistance,
			       const conductance_mapping *mapping,
			       const double *row_voltages,
			       double spike_amplitude, double *decoded_outputs);

void free_crossbar_output_matrix(Crossbar_Output_Matrix *result);

#endif
