#include "spires_interface.h"
#include <spires.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <math.h>

int collect_reservoir_states(
        spires_reservoir *reservoir,
        const double *input_series,
        size_t series_length,
        Reservoir_State_Matrix *result
) {
    //error checking
    //RIP

    //clear the result first 
    result->num_samples = 0;
    result->num_features = 0;
    result->states = NULL;

    const size_t num_inputs = spires_num_inputs(reservoir);

    const size_t num_neurons = spires_num_neurons(reservoir);

    if (series_length > SIZE_MAX / num_neurons || series_length * num_neurons 
            > SIZE_MAX / sizeof(double)) {
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
        if (status != SPIRES_OK){
            free(states);
            return -1;
        }

        double *current_state = &states[i * num_neurons];
        status = spires_read_reservoir_state(reservoir, current_state);
        if (status != SPIRES_OK){
            free(states);
            return -1;
        }
    }

    result->num_samples = series_length;
    result->num_features = num_neurons;
    result->states = states;

    return 0;
}

int map_signed_weights_to_resistance(
        const double *weights,
        size_t num_neurons,
        size_t num_outputs,
        double resistance_on,
        double resistance_off,
        double *resistances,
        double *conductance_offset,
        double *conductance_scale
        ) {
   //safety check arguments
   if (weights == NULL || resistances == NULL || num_neurons == 0 ||
        num_outputs == 0 || resistance_on <= 0 || resistance_off <= resistance_on) {
       return -1;
   }

   const size_t count = num_neurons * num_outputs;

    const double conductance_max = 1.0 / resistance_on;
    const double conductance_min = 1.0 / resistance_off;

    double max_absolute_weight = 0.0;

    for (size_t i = 0; i < count; i++) {
        double magnitude = fabs(weights[i]);

        if (magnitude > max_absolute_weight) {
            max_absolute_weight = magnitude;
        }
    }

    const double offset =
        0.5 * (conductance_max + conductance_min);

    if (max_absolute_weight == 0.0) {
        for (size_t i = 0; i < count; i++) {
            resistances[i] = 1.0 / offset;
        }

        if (conductance_offset != NULL) {
            *conductance_offset = offset;
        }

        if (conductance_scale != NULL) {
            *conductance_scale = 0.0;
        }

        return 0;
    }

    const double scale =
        (conductance_max - conductance_min) /
        (2.0 * max_absolute_weight);

    for (size_t i = 0; i < count; i++) {
        double conductance =
            offset + scale * weights[i];

        /*
         * Protect against small floating-point excursions.
         */
        if (conductance < conductance_min) {
            conductance = conductance_min;
        } else if (conductance > conductance_max) {
            conductance = conductance_max;
        }

        resistances[i] = 1.0 / conductance;
    }

    if (conductance_offset != NULL) {
        *conductance_offset = offset;
    }

    if (conductance_scale != NULL) {
        *conductance_scale = scale;
    }

    return 0;
}

int train_reservoir(
        spires_reservoir *reservoir,
        double *input_series,
        double *target_series,
        size_t series_length,
        double lambda
) {
    spires_status status = spires_train_ridge(
            reservoir, 
            (double *)input_series, 
            (double *)target_series,
            series_length, 
            lambda
    );

    if (status != SPIRES_OK) {
        fprintf(stderr, "Spires ridge training failed");
        return -1;
    }
    //need to figure out how to scale weights to conductance values,
    
    //that are then the reciprocal of the resistances
    
    //Also need to find some conversion for output column current, and target_series
    return 0;

}

void free_reservoir_state_matrix(Reservoir_State_Matrix *matrix) {
    if (!matrix) {
        return;
    }

    free(matrix->states);

    matrix->states = NULL;
    matrix->num_samples = 0;
    matrix->num_features = 0;
}
