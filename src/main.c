#include "spires_interface.h"
#include "crossbar_generator.h"
#include "read_crossbar.h"

#include <math.h>
#include <spires.h>
#include <stdio.h>
#include <stdlib.h>
#include <plplot/plplot.h>

#define NUM_NEURONS 400
#define NUM_INPUTS 4
#define NUM_OUTPUTS 2
#define NUM_TRAINING_STEPS 500
#define NUM_STEPS 2000

#define SPIKE_THRESHOLD 0.5
#define SPIKE_AMPLITUDE 0.1

#define PI 3.14159265358979323846

static int plot_raster(
    const Reservoir_State_Matrix *matrix,
    size_t neurons_to_plot,
    double spike_threshold
); 


static int print_software_outputs(
    spires_reservoir *reservoir,
    const double *input_series,
    size_t series_length,
    size_t num_inputs,
    size_t num_outputs,
    size_t samples_to_print,
    const char *label
);

int main(void) {
    //discrete LIF parameters for spires
    double lif_config[] = {
        0.0, //V_off
        1.0, //V_th
        0.2, //leak rate
        0.5, //bias
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
        .neuron_params = lif_config
    };

    spires_reservoir *reservoir = NULL;
    
    spires_status status = spires_reservoir_create(
            &config,
            &reservoir
    );

    if (status != SPIRES_OK) {
        fprintf(stderr, "Failed to create reservoir");
        return -1;
    }

    //create training inputs
    double training_inputs[NUM_TRAINING_STEPS * NUM_INPUTS];
    for (size_t timestep = 0; timestep < NUM_TRAINING_STEPS; timestep++) {
        for (size_t input = 0; input < NUM_INPUTS; input++) {
            training_inputs[timestep * NUM_INPUTS + input] = sin(2.0 * PI * (double)timestep / 50.0);
        }
    }
    
    //create target outputs
    double target_outputs[NUM_TRAINING_STEPS * NUM_OUTPUTS];
    for (size_t timestep = 0; timestep < NUM_TRAINING_STEPS; timestep++) {
        size_t next_timestep = (timestep + 1) % NUM_TRAINING_STEPS;
        double target = sin(2.0 * PI * (double)next_timestep / 50.0);
        for (size_t output = 0; output < NUM_OUTPUTS; output++) {
            target_outputs[timestep * NUM_OUTPUTS + output] = target;
        }
    }

    Reservoir_State_Matrix state_matrix = {0};
    if (collect_reservoir_states(reservoir, training_inputs, NUM_TRAINING_STEPS,
                &state_matrix) != 0) {
        fprintf(stderr, "Failed to collect reservoir states");
        spires_reservoir_destroy(reservoir);
        return -1;
    }
    printf("collected state matrix: %zu x %zu\n", state_matrix.num_samples,
            state_matrix.num_features);

    print_software_outputs(
        reservoir,
        training_inputs,
        NUM_TRAINING_STEPS,
        NUM_INPUTS,
        NUM_OUTPUTS,
        10,
        "Software outputs before training:"
    );

    //training the readout layer
    const double lambda = 1.0e-4;
    int training_status = train_reservoir(reservoir, training_inputs, 
            target_outputs, NUM_TRAINING_STEPS, lambda);
    if (training_status < 0) {
        fprintf(stderr, "Failed to train the reservoir");
        free_reservoir_state_matrix(&state_matrix);
        spires_reservoir_destroy(reservoir);
        return -1;
    }

    print_software_outputs(
        reservoir,
        training_inputs,
        NUM_TRAINING_STEPS,
        NUM_INPUTS,
        NUM_OUTPUTS,
        10,
        "Software outputs after training:"
    );

    //generate raster plot for verification
    if (plot_raster(&state_matrix, NUM_NEURONS, SPIKE_THRESHOLD) != 0) {
        fprintf(stderr, "Failed to plot raster\n");
    }

    //fill out initial resistances
    double *initial_resistances = malloc(NUM_NEURONS * NUM_OUTPUTS * sizeof(*initial_resistances));
    if (!initial_resistances) {
        fprintf(stderr, "Failed to allocate memory for initial resistances");
        return -1;
    }

    //resistances are inversely proportional to the software weigts
    for (size_t i = 0; i < NUM_NEURONS * NUM_OUTPUTS; i++) {
        initial_resistances[i] = 80000;
    }

    //convert continous states to spikes
    double *spikes_voltages = malloc(state_matrix.num_features * 
            state_matrix.num_samples * sizeof(*spikes_voltages));

    if (spikes_voltages == NULL) {
        fprintf(stderr, "Failed to allocate spikes voltages");
        free(initial_resistances);
        free_reservoir_state_matrix(&state_matrix);
        spires_reservoir_destroy(reservoir);
        return -1;
    }

    for (size_t sample = 0; sample < state_matrix.num_samples; sample++) {
        for (size_t neuron = 0; neuron < state_matrix.num_features; neuron++) {
           size_t index = sample * state_matrix.num_features + neuron; 

           spikes_voltages[index] = 
               state_matrix.states [index] > SPIKE_THRESHOLD ? SPIKE_AMPLITUDE : 0.0;
        }
    }

    const Crossbar_Config crossbar_config = {
        .rows = state_matrix.num_features,
        .columns = NUM_OUTPUTS,
        .input_series = spikes_voltages,
        .num_samples = state_matrix.num_samples,
        .initial_resistance = initial_resistances,
        .model_path = "hp_memristor.cir",
        .subcircuit_name = "memristor",
        .load_resistance = 50.0,
        .time_step = 1e-6,
        .stop_time = state_matrix.num_samples * 1e-6,
        .print_state_nodes = 0
    };

    if (generate_crossbar("crossbar.cir", &crossbar_config) < 0) {
        fprintf(stderr, "failed to create crossbar config");
        free(initial_resistances);
        free_reservoir_state_matrix(&state_matrix);
        spires_reservoir_destroy(reservoir);
        return -1;
    }
    printf("Generated crossbar!!");

    //call ngspice for crossbar
    if (run_ngspice("crossbar.cir") < 0) {
        fprintf(stderr, "Failed to run_ngspice");
        free(initial_resistances);
        free_reservoir_state_matrix(&state_matrix);
        spires_reservoir_destroy(reservoir);
        return -1;
    }
    printf("ran ngspice!!");

    //crossbar parameters needed for reading
    Crossbar_Output_Matrix crossbar_output = {
        .num_samples = NUM_TRAINING_STEPS, //this isnt right?
        .num_outputs = NUM_OUTPUTS,
        .time = NULL,
        .voltages = NULL
    };
    
    if (read_crossbar("crossbar_output.dat", NUM_OUTPUTS, &crossbar_output) < 0) {
        fprintf(stderr, "Failed to read crossbar output file");
        free(initial_resistances);
        free_reservoir_state_matrix(&state_matrix);
        spires_reservoir_destroy(reservoir);
        free_crossbar_output_matrix(&crossbar_output);
        return -1;
    }
    printf("read the crossbar outputs!!");

    //printing for testing purposes 
    // printf("Read data:\n"); 
    // for (size_t sample = 0; sample < crossbar_output.num_samples; sample++) {
    //     printf("%f", crossbar_output.time[sample]);
    //     for (size_t output = 0; output < crossbar_output.num_outputs; output++) {
    //         printf(" %f", 
    //                 crossbar_output.voltages[sample * crossbar_output.num_outputs + output]);
    //     }
    //     printf("\n");
    // }

    printf("YAY IT WORKED!!! Cleaning up :)");
    free(initial_resistances);
    free(spikes_voltages);
    free_reservoir_state_matrix(&state_matrix);
    spires_reservoir_destroy(reservoir);
    free_crossbar_output_matrix(&crossbar_output);

    return 0;
}

static int plot_raster(
    const Reservoir_State_Matrix *matrix,
    size_t neurons_to_plot,
    double spike_threshold
) {
    if (!matrix || !matrix->states || matrix->num_samples == 0) {
        return -1;
    }

    if (neurons_to_plot > matrix->num_features) {
        neurons_to_plot = matrix->num_features;
    }

    //count spikes
    size_t spike_count = 0;
    for (size_t t = 0; t < matrix->num_samples; t++) {
        for (size_t n = 0; n < neurons_to_plot; n++) {
            double value = matrix->states[t * matrix->num_features + n];
            if (value > spike_threshold) {
                spike_count++;
            }
        }
    }

    if (spike_count == 0) {
        fprintf(stderr, "No spikes found above threshold %.3f\n", spike_threshold);
        return -1;
    }

    PLFLT *x = malloc(spike_count * sizeof(*x));
    PLFLT *y = malloc(spike_count * sizeof(*y));
    if (!x || !y) {
        free(x);
        free(y);
        return -1;
    }

    //fill spike coordinates
    size_t k = 0;
    for (size_t t = 0; t < matrix->num_samples; t++) {
        for (size_t n = 0; n < neurons_to_plot; n++) {
            double value = matrix->states[t * matrix->num_features + n];
            if (value > spike_threshold) {
                x[k] = (PLFLT)t;
                y[k] = (PLFLT)n;
                k++;
            }
        }
    }

    //output to png
    plsdev("svg");
    plsfnam("reservoir_raster.svg");
    
    plsetopt("geometry", "1600x1200");
    plscolbg(255, 255, 255);

    plinit();

    plscol0(1, 40, 40, 40); //gray axis
    plscol0(2, 0, 0, 0); //blue points

    plcol0(1);
    plwidth(1.0);

    plenv(
        0.0,
        (PLFLT)(matrix->num_samples - 1),
        0.0,
        (PLFLT)(neurons_to_plot - 1),
        0,
        0
    );

    pllab(
        "Timestep",
        "Neuron index",
        "SPIRES Reservoir Raster Plot"
    );

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

static int print_software_outputs(
    spires_reservoir *reservoir,
    const double *input_series,
    size_t series_length,
    size_t num_inputs,
    size_t num_outputs,
    size_t samples_to_print,
    const char *label
)
{
    if (!reservoir || !input_series || !label) {
        return -1;
    }

    double *output = malloc(num_outputs * sizeof(*output));
    if (!output) {
        return -1;
    }

    if (spires_reservoir_reset(reservoir) != SPIRES_OK) {
        free(output);
        return -1;
    }

    if (samples_to_print > series_length) {
        samples_to_print = series_length;
    }

    printf("\n%s\n", label);

    for (size_t timestep = 0;
         timestep < series_length;
         timestep++) {

        const double *current_input =
            &input_series[timestep * num_inputs];

        if (spires_step(reservoir, current_input) != SPIRES_OK) {
            free(output);
            return -1;
        }

        if (spires_compute_output(reservoir, output) != SPIRES_OK) {
            free(output);
            return -1;
        }

        if (timestep < samples_to_print) {
            printf("timestep %zu:", timestep);

            for (size_t output_index = 0;
                 output_index < num_outputs;
                 output_index++) {

                printf(
                    " output[%zu]=%+.8e",
                    output_index,
                    output[output_index]
                );
            }

            printf("\n");
        }
    }

    free(output);
    return 0;
}
