#ifndef BENCHMARK_H
#define BENCHMARK_H

#include "crossbar_generator.h"
#include "read_crossbar.h"
#include "spires_interface.h"
#include <stddef.h>

typedef struct {
	const char *model_path;
	const char *subcircuit_name;
} MemModel;

int run_benchmark(const spires_reservoir_config *config,
		  spires_reservoir *reservoir,
		  Reservoir_State_Matrix *state_matrix, const char *model_path,
		  const char *subcircuit_name, double *predictions_out);

double calculate_MSE(const double *expected, const double *predicted,
		     const size_t num_steps, const size_t num_outputs);

int plot_raster(const Reservoir_State_Matrix *matrix, size_t neurons_to_plot,
		double spike_threshold);

int plot_reservoir_predictions(const double *expected, const double *predicted,
			       size_t num_samples, size_t num_outputs,
			       size_t output_to_plot, const char *model_path);

int plot_model_delta(const double *fixed, const double *model,
		     size_t num_samples, size_t num_outputs,
		     size_t output_to_plot, const char *model_path);

int plot_all_model_deltas(const double *predictions, const MemModel *models,
			  size_t model_count, size_t num_samples,
			  size_t num_outputs, size_t output_to_plot);

#endif
