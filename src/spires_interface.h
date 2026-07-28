#ifndef SPIRES_BACKEND_H
#define SPIRES_BACKEND_H

#include <stddef.h>
#include <spires.h>

typedef struct {
    size_t num_samples; //number of time steps
    size_t num_features;
    double *states;
} Reservoir_State_Matrix;

int collect_reservoir_states(
    spires_reservoir *reservoir,
    const double *input_series,
    size_t series_length,
    Reservoir_State_Matrix *result
);

int map_signed_weights_to_resistance(
        const double *weights,
        size_t num_neurons,
        size_t num_outputs,
        double resistance_on,
        double resistance_off,
        double *resistances,
        double *conductance_offset,
        double *conductance_scale
);

int train_reservoir(
        spires_reservoir *reservoir,
        double *input_series,
        double *taret_series,
        size_t series_length,
        double lambda
);

void free_reservoir_state_matrix(
        Reservoir_State_Matrix *matrix
);

#endif
