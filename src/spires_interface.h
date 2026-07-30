#ifndef SPIRES_INTERFACE_H
#define SPIRES_INTERFACE_H

#include <spires.h>
#include <stddef.h>

typedef struct {
	size_t num_samples; // number of time steps
	size_t num_features;
	double *states;
} Reservoir_State_Matrix;

typedef struct {
	double g_min;
	double g_max;
	double alpha;
	double max_abs_weight;
} conductance_mapping;

int collect_reservoir_states(spires_reservoir *reservoir,
			     const double *input_series, size_t series_length,
			     Reservoir_State_Matrix *result);

int train_reservoir(spires_reservoir *reservoir, double *input_series,
		    double *taret_series, size_t series_length, double lambda);

int convert_weights_to_resistances(const spires_reservoir *reservoir,
				   size_t num_neurons, size_t num_outputs,
				   double r_on, double r_off,
				   double **resistances_out,
				   conductance_mapping *mapping);

void free_reservoir_state_matrix(Reservoir_State_Matrix *matrix);

#endif
