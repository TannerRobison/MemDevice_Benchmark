#include "crossbar_generator.h"
#include <stdio.h>

static int validate_config(const Crossbar_Config *config)
{
	printf("validating configs...\n");
	if (config == NULL) {
		return -1;
	}
	if (config->rows == 0) {
		return -1;
	}
	if (config->columns == 0) {
		return -1;
	}
	if (config->input_series == NULL) {
		return -1;
	}
	if (config->num_samples == 0) {
		return -1;
	}
	if (config->initial_resistance == NULL) {
		return -1;
	}
	if (config->model_path == NULL) {
		return -1;
	}
	if (config->subcircuit_name == NULL) {
		return -1;
	}
	if (config->load_resistance <= 0.0) {
		return -1;
	}
	if (config->time_step <= 0.0) {
		return -1;
	}
	if (config->stop_time <= 0.0) {
		return -1;
	}
	if (config->print_state_nodes != 0 && config->print_state_nodes != 1) {
		return -1;
	}
	return 0;
}

// thus starts fprintf hell...
static int write_header(FILE *file, const Crossbar_Config *config)
{
	if (fprintf(file,
		    "* Generating a crossbar with:\n"
		    "*Rows: %zu\n"
		    "*Columns: %zu\n",
		    config->rows, config->columns) < 0) {
		fprintf(stderr, "Failed to write header at crossbar size");
		return -1;
	}

	// include the memristor model
	if (fprintf(file, "\n.include \"%s\"\n", config->model_path) < 0) {
		fprintf(stderr, "Faile to write header at .include memristor");
		return -1;
	}

	return 0;
}

static int write_input_series(FILE *file, const Crossbar_Config *config)
{
	fprintf(file, "\n* SPIRES reservoir states\n");

	for (size_t row = 0; row < config->rows; row++) {
		fprintf(file, "VROW%zu row%zu 0 PWL(", row, row);

		for (size_t sample = 0; sample < config->num_samples;
		     sample++) {
			double time = sample * config->time_step;
			double state =
			    config->input_series[sample * config->rows + row];

			double voltage = state;

			fprintf(file, "%.17g %.17g ", time, voltage);
		}
		fprintf(file, ")\n");
	}
	return ferror(file) ? -1 : 0;
}

static int write_memristor_array(FILE *file, const Crossbar_Config *config)
{
	if (fprintf(file, "\n* Memristor Array\n") < 0) {
		fprintf(stderr,
			"Failed to write memristor array comment label thing");
		return -1;
	}

	for (size_t row = 0; row < config->rows; row++) {
		for (size_t column = 0; column < config->columns; column++) {
			// initial_resistance here is done wrong, need to fix
			if (fprintf(file,
				    "X%zu%zu row%zu col%zu %s"
				    " PARAMS: Rinit=%.12g\n",
				    row, column, row, column,
				    config->subcircuit_name,
				    config->initial_resistance
					[row * config->columns + column]) < 0) {
				fprintf(stderr,
					"Failed to write memristor array");
				return -1;
			}
		}
	}
	return 0;
}

static int write_column_loads(FILE *file, const Crossbar_Config *config)
{
	if (fprintf(file, "\n* Column Loads\n") < 0) {
		fprintf(stderr,
			"Failed to write column loads comment label thing");
	}

	for (size_t column = 0; column < config->columns; column++) {
		if (fprintf(file, "RLOAD%zu col%zu 0 %.0f\n", column, column,
			    config->load_resistance) < 0) {
			fprintf(stderr, "Failed to write column loads");
			return -1;
		}
	}
	return 0;
}

static int write_simulation(FILE *file, const Crossbar_Config *config)
{
	if (fprintf(file, "\n* Simulation\n") < 0) {
		fprintf(stderr, "Failed to write simulation header");
		return -1;
	}

	if (fprintf(file, ".tran %.17g %.17g uic\n.control\nrun\n",
		    config->time_step, config->stop_time) < 0) {
		fprintf(stderr, "Failed to write simulation command");
		return -1;
	}
	return 0;
}

static int write_save_date(FILE *file, const Crossbar_Config *config)
{
	fprintf(file, "linearize");
	for (int column = 0; column < config->columns; column++) {
		if (fprintf(file, " v(col%zu)", column) < 0) {
			fprintf(stderr, "Failed to write linearize statements");
			return -1;
		}
	}

	fprintf(file, "\nwrdata output/crossbar_output.dat");
	for (int column = 0; column < config->columns; column++) {
		fprintf(file, " v(col%zu)",
			column); // ive given up on error checking
				 // Im doing too much of it
	}

	if (fprintf(file, "\n.endc\n.end") < 0) {
		fprintf(stderr, "Failed to write end statements");
		return -1;
	}
	return 0;
}

int generate_crossbar(const char *output_filename,
		      const Crossbar_Config *config)
{
	if (validate_config(config) < 0) {
		fprintf(stderr, "invalid config");
		return -1;
	}

	FILE *file = fopen(output_filename, "w");

	if (file == NULL) {
		fprintf(stderr, "could not create file");
		return -1;
	}

	if (write_header(file, config) != 0 ||
	    write_input_series(file, config) != 0 ||
	    write_memristor_array(file, config) != 0 ||
	    write_column_loads(file, config) != 0 ||
	    write_simulation(file, config) != 0 ||
	    write_save_date(file, config) != 0) {

		fclose(file);
		return -1;
	}

	if (fclose(file) != 0) {
		fprintf(stderr, "failed to close output file");
		return -1;
	}
	return 0;
}
