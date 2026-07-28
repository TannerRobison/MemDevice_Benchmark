#include <../src/neurons/lif_discrete.h>
#include <math.h>
#include <spires.h>
#include <stdio.h>
#include <stdlib.h>

#define N_TRAIN 500
#define N_TEST 100
#define PI 3.14159265358979323846

int main(void) {
  /* 1. Configure the reservoir */
  double lif_cfg[] = {0.0, 1.0, 0.2, 0.5};

  spires_reservoir_config cfg = {
      .num_neurons = 400,
      .num_inputs = 1,
      .num_outputs = 1,
      .spectral_radius = 0.95,
      .ei_ratio = 0.8,
      .input_strength = 0.1,
      .connectivity = 0.1,
      .dt = 1.0,
      .connectivity_type = SPIRES_CONN_RANDOM,
      .neuron_type = SPIRES_NEURON_LIF_DISCRETE,
      .neuron_params = lif_cfg,
  };

  /* 2. Create the reservoir */
  spires_reservoir *r = NULL;
  spires_status s = spires_reservoir_create(&cfg, &r);
  if (s != SPIRES_OK) {
    fprintf(stderr, "Failed to create reservoir: %d\n", s);
    return 1;
  }

  /* 3. Generate training data — predict sin(t+1) from sin(t) */
  double input_train[N_TRAIN];
  double target_train[N_TRAIN];
  for (int i = 0; i < N_TRAIN; i++) {
    input_train[i] = sin(2.0 * PI * i / 50.0);
    target_train[i] = sin(2.0 * PI * (i + 1) / 50.0);
  }

  /* 4. Train with ridge regression */
  s = spires_train_ridge(r, input_train, target_train, N_TRAIN, 1e-6);

  if (s != SPIRES_OK) {
    fprintf(stderr, "Training failed: %d\n", s);
    spires_reservoir_destroy(r);
    return 1;
  }

  /* 5. Generate test input */
  double input_test[N_TEST];
  for (int i = 0; i < N_TEST; i++) {
    input_test[i] = sin(2.0 * PI * (N_TRAIN + i) / 50.0);
  }

  /* 6. Run inference */
  double *predictions = spires_run(r, input_test, N_TEST);
  if (!predictions) {
    fprintf(stderr, "Inference failed\n");
    spires_reservoir_destroy(r);
    return 1;
  }

  /* 7. Print a few predictions vs. expected values */
  printf("Step | Predicted | Expected\n");
  printf("-----+-----------+---------\n");
  for (int i = 0; i < 10; i++) {
    double expected = sin(2.0 * PI * (N_TRAIN + i + 1) / 50.0);
    printf("%4d | %+.5f | %+.5f\n", i, predictions[i], expected);
  }

  /* 8. Clean up */
  free(predictions);
  spires_reservoir_destroy(r);
  return 0;
}
