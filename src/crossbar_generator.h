#ifndef CROSSBAR_GENERATOR_H
#define CROSSBAR_GENERATOR_H

#include <stddef.h>

typedef struct {
	size_t rows;	// number of neurons
	size_t columns; // number of outputs

	// array of row input voltages.
	// length must equal rows (# of rows)
	const double *input_series;
	size_t num_samples;

	// array of initial resistances
	// stored in row-major order
	// size must be rows * colums
	const double *initial_resistance;

	// path to the memory component path
	const char *model_path;

	// name of subcircuit(specific model)
	const char *subcircuit_name;

	// resistance from each column to ground
	double load_resistance;

	// transient sim settings
	double time_step;
	double stop_time;

	// 1 = true | 0 = false
	int print_state_nodes;
} Crossbar_Config;

int generate_crossbar(const char *output_filename,
		      const Crossbar_Config *config);

#endif
