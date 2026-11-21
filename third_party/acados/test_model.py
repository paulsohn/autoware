# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""A scalar integrator exercises symbolic generation and the native QP solver."""
import os
from pathlib import Path

from acados_template import AcadosModel, AcadosOcp, AcadosOcpSolver
from casadi import SX
import numpy as np

# Code-generation tools must load their declared GNU ABI, not a host installation.
runtime_files = {file.resolve() for directory in os.environ['LD_LIBRARY_PATH'].split(os.pathsep)
                 for file in Path(directory).glob('*.so*')}
maps = Path('/proc/self/maps').read_text().splitlines()
for soname in ['libstdc++.so', 'libgcc_s.so']:
    paths = {Path(line.rsplit(None, 1)[-1]).resolve() for line in maps if soname in line}
    assert paths and paths <= runtime_files, (soname, paths, runtime_files)

model = AcadosModel()
model.name = 'integrator'
model.x = SX.sym('x')
model.xdot = SX.sym('xdot')
model.u = SX.sym('u')
model.f_expl_expr = model.u
model.f_impl_expr = model.xdot - model.u
ocp = AcadosOcp()
ocp.model = model
ocp.solver_options.N_horizon = 20
ocp.solver_options.tf = 2.0
ocp.solver_options.qp_solver = 'FULL_CONDENSING_HPIPM'
ocp.solver_options.nlp_solver_type = 'SQP'
ocp.solver_options.integrator_type = 'ERK'
ocp.cost.cost_type = 'LINEAR_LS'
ocp.cost.cost_type_e = 'LINEAR_LS'
ocp.cost.W = np.diag([1.0, 0.01])
ocp.cost.W_e = np.array([[10.0]])
ocp.cost.Vx = np.array([[1.0], [0.0]])
ocp.cost.Vu = np.array([[0.0], [1.0]])
ocp.cost.Vx_e = np.array([[1.0]])
ocp.cost.yref = np.zeros(2)
ocp.cost.yref_e = np.zeros(1)
ocp.constraints.x0 = np.array([1.0])
AcadosOcpSolver.generate(ocp, json_file='acados_ocp.json')
