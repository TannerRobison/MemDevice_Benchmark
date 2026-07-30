#include "crossbar_generator.h"
#include "read_crossbar.h"
#include "spires_interface.h"

#include <math.h>
#include <plplot/plplot.h>
#include <spires.h>
#include <stdio.h>
#include <stdlib.h>

#define NUM_NEURONS 400
#define NUM_INPUTS 4
#define NUM_OUTPUTS 2
#define NUM_CROSSBAR_COLUMNS (NUM_OUTPUTS * 2)
#define NUM_TRAINING_STEPS 500
#define NUM_STEPS 2000

#define SPIKE_THRESHOLD 0.9
#define SPIKE_AMPLITUDE 0.1

#define PI 3.14159265358979323846

static int plot_raster(const Reservoir_State_Matrix *matrix,
		       size_t neurons_to_plot, double spike_threshold);

static int plot_reservoir_predictions(const double *expected,
				      const double *predicted,
				      size_t num_samples, size_t num_outputs,
				      size_t output_to_plot);

int main(void)
{
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
	    .spectral_radius = 0.95,
	    .ei_ratio = 0.8,
	    .input_strength = 0.1,
	    .connectivity = 0.1,
	    .dt = 1.0,
	    .connectivity_type = SPIRES_CONN_RANDOM,
	    .neuron_type = SPIRES_NEURON_LIF_DISCRETE,
	    .neuron_params = lif_config};

	spires_reservoir *reservoir = NULL;

	spires_status status = spires_reservoir_create(&config, &reservoir);

	if (status != SPIRES_OK) {
		fprintf(stderr, "Failed to create reservoir");
		return -1;
	}

	// create training inputs
	double training_inputs[NUM_TRAINING_STEPS * NUM_INPUTS];
	for (size_t timestep = 0; timestep < NUM_TRAINING_STEPS; timestep++) {
		for (size_t input = 0; input < NUM_INPUTS; input++) {
			training_inputs[timestep * NUM_INPUTS + input] =
			    sin(2.0 * PI * (double)timestep / 50.0);
		}
	}

	// create target outputs
	double target_outputs[NUM_TRAINING_STEPS * NUM_OUTPUTS];
	for (size_t timestep = 0; timestep < NUM_TRAINING_STEPS; timestep++) {
		size_t next_timestep = (timestep + 1) % NUM_TRAINING_STEPS;
		double target = sin(2.0 * PI * (double)next_timestep / 50.0);
		for (size_t output = 0; output < NUM_OUTPUTS; output++) {
			target_outputs[timestep * NUM_OUTPUTS + output] =
			    target;
		}
	}

	Reservoir_State_Matrix state_matrix = {0};
	if (collect_reservoir_states(reservoir, training_inputs,
				     NUM_TRAINING_STEPS, &state_matrix) != 0) {
		fprintf(stderr, "Failed to collect reservoir states");
		spires_reservoir_destroy(reservoir);
		return -1;
	}
	printf("collected state matrix: %zu x %zu\n", state_matrix.num_samples,
	       state_matrix.num_features);

	// training the readout layer
	const double lambda = 1.0e-4;
	int training_status =
	    train_reservoir(reservoir, training_inputs, target_outputs,
			    NUM_TRAINING_STEPS, lambda);
	if (training_status < 0) {
		fprintf(stderr, "Failed to train the reservoir");
		free_reservoir_state_matrix(&state_matrix);
		spires_reservoir_destroy(reservoir);
		return -1;
	}

	// generate raster plot for verification
	if (plot_raster(&state_matrix, NUM_NEURONS, SPIKE_THRESHOLD) != 0) {
		fprintf(stderr, "Failed to plot raster\n");
	}

	// copy readout weights and convert to conductances
	double *initial_resistances = NULL;
	conductance_mapping mapping;

	if (convert_weights_to_resistances(
		reservoir, NUM_NEURONS, NUM_OUTPUTS, 1000.0, 100000.0,
		&initial_resistances, &mapping) != 0) {
		return -1;
	}

	double *row_voltages =
	    malloc(state_matrix.num_features * state_matrix.num_samples *
		   sizeof(double));

	if (row_voltages == NULL) {
		fprintf(stderr, "Failed to allocate spikes voltages");
		free(initial_resistances);
		free_reservoir_state_matrix(&state_matrix);
		spires_reservoir_destroy(reservoir);
		return -1;
	}

	for (size_t sample = 0; sample < state_matrix.num_samples; sample++) {
		for (size_t neuron = 0; neuron < state_matrix.num_features;
		     neuron++) {
			size_t index =
			    sample * state_matrix.num_features + neuron;

			row_voltages[index] =
			    SPIKE_AMPLITUDE * state_matrix.states[index];
		}
	}

	const Crossbar_Config crossbar_config = {
	    .rows = state_matrix.num_features,
	    .columns = NUM_CROSSBAR_COLUMNS,
	    .input_series = row_voltages,
	    .num_samples = state_matrix.num_samples,
	    .initial_resistance = initial_resistances,
	    .model_path = "models/hp_memristor.cir",
	    .subcircuit_name = "memristor",
	    .load_resistance = 50.0,
	    .time_step = 1e-6,
	    .stop_time = state_matrix.num_samples * 1e-6,
	    .print_state_nodes = 0};

	if (generate_crossbar("output/crossbar.cir", &crossbar_config) < 0) {
		fprintf(stderr, "failed to create crossbar config");
		free(initial_resistances);
		free_reservoir_state_matrix(&state_matrix);
		spires_reservoir_destroy(reservoir);
		return -1;
	}
	printf("Generated crossbar!!");

	// call ngspice for crossbar
	if (run_ngspice("output/crossbar.cir") < 0) {
		fprintf(stderr, "Failed to run_ngspice");
		free(initial_resistances);
		free_reservoir_state_matrix(&state_matrix);
		spires_reservoir_destroy(reservoir);
		return -1;
	}
	printf("ran ngspice!!");

	// crossbar parameters needed for reading
	Crossbar_Output_Matrix crossbar_output = {
	    .num_samples = NUM_TRAINING_STEPS,
	    .num_outputs = NUM_CROSSBAR_COLUMNS,
	    .time = NULL,
	    .voltages = NULL};

	if (read_crossbar("output/crossbar_output.dat", NUM_CROSSBAR_COLUMNS,
			  &crossbar_output) < 0) {
		fprintf(stderr, "Failed to read crossbar output file");
		free(initial_resistances);
		free_reservoir_state_matrix(&state_matrix);
		spires_reservoir_destroy(reservoir);
		free_crossbar_output_matrix(&crossbar_output);
		return -1;
	}
	printf("read the crossbar outputs!!\n");

	double *decoded_outputs =
	    malloc(NUM_OUTPUTS * NUM_TRAINING_STEPS * sizeof(double));
	if (decoded_outputs == NULL) {
		fprintf(stderr,
			"Failed to allocate memory for decoded outputs");
	}
	if (convert_output_to_software(
		NUM_NEURONS, NUM_OUTPUTS, NUM_TRAINING_STEPS,
		crossbar_output.voltages, initial_resistances,
		crossbar_config.load_resistance, &mapping, row_voltages,
		SPIKE_AMPLITUDE, decoded_outputs) < 0) {
		fprintf(stderr,
			"Failed to convert crossbar outputs back to software");
		return -1;
	}

	// comparing prediction
	for (int i = 0; i < NUM_TRAINING_STEPS; i++) {
		printf("expected: %g , predicted: %g\n", target_outputs[i],
		       decoded_outputs[i]);
	}

	// plotting expected vs. prediction
	plot_reservoir_predictions(target_outputs, decoded_outputs,
				   NUM_TRAINING_STEPS, NUM_OUTPUTS, 0);

	printf("YAY IT WORKED!!! Cleaning up :)");
	free(initial_resistances);
	// free(spikes_voltages);
	free(row_voltages);
	free(decoded_outputs);
	free_reservoir_state_matrix(&state_matrix);
	spires_reservoir_destroy(reservoir);
	free_crossbar_output_matrix(&crossbar_output);

	return 0;
}

static int plot_raster(const Reservoir_State_Matrix *matrix,
		       size_t neurons_to_plot, double spike_threshold)
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

	// output to png
	plsdev("svg");
	plsfnam("output/reservoir_raster.svg");

	plsetopt("geometry", "1600x1200");
	plscolbg(255, 255, 255);

	plinit();

	plscol0(1, 40, 40, 40); // gray axis
	plscol0(2, 0, 0, 0);	// blue points

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

static int plot_reservoir_predictions(const double *expected,
				      const double *predicted,
				      size_t num_samples, size_t num_outputs,
				      size_t output_to_plot)
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
	plsfnam("output/reservoir_prediction.svg");
	plsetopt("geometry", "1600x1000");

	plscolbg(255, 255, 255);
	plinit();

	plscol0(1, 0, 0, 0);
	plscol0(2, 30, 90, 200);
	plscol0(3, 200, 50, 50);

	plcol0(1);
	plwidth(1.0);

	plenv(0.0, (PLFLT)(num_samples - 1), y_min, y_max, 0, 0);

	pllab("Timestep", "Output", "Expected vs SPICE Crossbar Prediction");

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
