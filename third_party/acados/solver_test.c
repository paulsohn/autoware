// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include "c_generated_code/acados_solver_integrator.h"

#include <math.h>
#include <stdio.h>

int main(void)
{
  integrator_solver_capsule * solver = integrator_acados_create_capsule();
  if (!solver || integrator_acados_create(solver) != 0) return 1;
  const int status = integrator_acados_solve(solver);
  double initial, terminal;
  ocp_nlp_config * config = integrator_acados_get_nlp_config(solver);
  ocp_nlp_dims * dims = integrator_acados_get_nlp_dims(solver);
  ocp_nlp_out * out = integrator_acados_get_nlp_out(solver);
  ocp_nlp_out_get(config, dims, out, 0, "x", &initial);
  ocp_nlp_out_get(config, dims, out, 20, "x", &terminal);
  const int failed =
    status != 0 || !isfinite(terminal) || fabs(initial - 1.0) > 1e-6 || fabs(terminal) > 0.01;
  printf("solver_status=%d x0=%g terminal=%g\n", status, initial, terminal);
  integrator_acados_free(solver);
  integrator_acados_free_capsule(solver);
  return failed;
}
