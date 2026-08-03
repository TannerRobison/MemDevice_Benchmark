#include "benchmark.h"

#include <dirent.h>
#include <math.h>
#include <spires.h>
#include <stdio.h>
#include <stdlib.h>

// spires reservoir parameters
#define NUM_NEURONS 600
#define NUM_INPUTS 1
#define NUM_OUTPUTS 1
#define SPECTRAL_RADIUS 0.95
#define EI_RATIO 0.8
#define INPUT_STRENGTH 0.1
#define CONNECTIVITY 0.1
#define DT 1.0

// ridge regression training
#define PI 3.14159265358979323846
#define LAMBDA 1.0e-4

#define NUM_TRAINING_STEPS 500
#define NUM_CROSSBAR_COLUMNS (NUM_OUTPUTS * 2)
// #define NUM_STEPS 2000

int main(void)
{
	// get number of files in model directory
	// This can be replaced later in for loop with number of models to test
	// size_t model_count = 0;
	// DIR *dirp;
	// struct dirent *entry;
	//
	// dirp = opendir("models");
	// while ((entry = readdir(dirp)) != NULL) {
	// 	if (entry->d_type == DT_REG) {
	// 		model_count++;
	// 	}
	// }
	// closedir(dirp);

	/* ---------- LIST ALL MODELS HERE ----------*/
	MemModel models[] = {
	    {
		.model_path = "models/hp_memristor.cir",
		.subcircuit_name = "memristor",
	    },
	    {
		.model_path = "models/fixed_resistor.cir",
		.subcircuit_name = "fixed_resistor",
	    },
	};

	size_t model_count = sizeof(models) / sizeof(models[0]);

	// MemModel hp_memristor = {.model_path = "models/hp_memristor.cir",
	// 			 .subcircuit_name = "memristor"};
	//
	// MemModel normal_resistor = {.model_path =
	// "models/fixed_resistor.cir", .subcircuit_name = "fixed_resistor"};
	//
	// MemModel *models = calloc(model_count, sizeof(MemModel));
	// if (models == NULL) {
	// 	fprintf(stderr, "Failed to allocate for model list");
	// 	return -1;
	// }

	/* ---------- SPIRES SET UP ----------*/
	// discrete LIF parameters for spires
	double lif_config[] = {
	    0.0, // V_off
	    1.0, // V_th
	    0.2, // leak rate
	    0.5, // bias
	};

	const spires_reservoir_config config = {
	    .num_neurons = NUM_NEURONS,
	    .num_inputs = NUM_INPUTS,
	    .num_outputs = NUM_OUTPUTS,
	    .spectral_radius = SPECTRAL_RADIUS,
	    .ei_ratio = EI_RATIO,
	    .input_strength = INPUT_STRENGTH,
	    .connectivity = CONNECTIVITY,
	    .dt = DT,
	    .connectivity_type = SPIRES_CONN_RANDOM,
	    .neuron_type = SPIRES_NEURON_LIF_DISCRETE,
	    .neuron_params = lif_config};

	spires_reservoir *reservoir = NULL;
	if (spires_reservoir_create(&config, &reservoir) != 0) {
		fprintf(stderr, "Failed to create reservoir");
		return -1;
	}

	/* ---------- Train spires readout layer ----------*/
	/* ---------- Training inputs ----------*/
	double training_inputs[NUM_TRAINING_STEPS * NUM_INPUTS];
	for (size_t timestep = 0; timestep < NUM_TRAINING_STEPS; timestep++) {
		for (size_t input = 0; input < NUM_INPUTS; input++) {
			double signal =
			    0.7 * sin(2.0 * PI * (double)timestep / 50.0) +
			    0.3 * sin(2.0 * PI * (double)timestep / 17.0);
			training_inputs[timestep * NUM_INPUTS + input] = signal;
		}
	}

	/* ---------- Target outputs  ----------*/
	double target_outputs[NUM_TRAINING_STEPS * NUM_OUTPUTS];
	for (size_t timestep = 0; timestep < NUM_TRAINING_STEPS; timestep++) {
		size_t next_timestep = (timestep + 1) % NUM_TRAINING_STEPS;

		double target =
		    0.7 * sin(2.0 * PI * (double)next_timestep / 50.0) +
		    0.3 * sin(2.0 * PI * (double)next_timestep / 17.0);
		for (size_t output = 0; output < NUM_OUTPUTS; output++) {
			target_outputs[timestep * NUM_OUTPUTS + output] =
			    target;
		}
	}

	/* ---------- Ridge Regression ----------*/
	if (train_reservoir(reservoir, training_inputs, target_outputs,
			    NUM_TRAINING_STEPS, LAMBDA) < 0) {
		fprintf(stderr, "Failed to train the spires reservoir");
		spires_reservoir_destroy(reservoir);
		return -1;
	}

	/* ---------- Collect reservoir states ----------*/
	Reservoir_State_Matrix state_matrix = {0};
	if (collect_reservoir_states(reservoir, training_inputs,
				     NUM_TRAINING_STEPS, &state_matrix) != 0) {
		fprintf(stderr, "Failed to collect reservoir states");
		spires_reservoir_destroy(reservoir);
		return -1;
	}
	printf("collected state matrix size: %zu x %zu\n",
	       state_matrix.num_samples, state_matrix.num_features);

	/* ---------- Generate raster plot ----------*/
	if (plot_raster(&state_matrix, NUM_NEURONS, 0.5) != 0) {
		fprintf(stderr, "Failed to plot raster");
	}

	/* ---------- Run Benchmark on each model ----------*/
	for (size_t i = 0; i < model_count; i++) {
		printf("Running benchmark on %s\n", models[i].model_path);
		if (run_benchmark(&config, reservoir, &state_matrix,
				  target_outputs, models[i].model_path,
				  models[i].subcircuit_name) < 0) {
			fprintf(stderr, "Failed to run benchmark");
			spires_reservoir_destroy(reservoir);
			return -1;
		}
	}

	free_reservoir_state_matrix(&state_matrix);
	spires_reservoir_destroy(reservoir);

	return 0;
}
