// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0

#include <autoware/learning_based_vehicle_model/interconnected_model.hpp>
#include <autoware/learning_based_vehicle_model/model_connections_helpers.hpp>

#include <gtest/gtest.h>
#include <pybind11/embed.h>

#include <vector>

namespace model = autoware::simulator::learning_based_vehicle_model;

TEST(LearnedVehicleModel, NameMappingAndUnconnectedSignals)
{
  char acceleration[] = "acceleration";
  char position[] = "position";
  char missing[] = "missing";
  const auto mapping =
    model::createConnectionsMap({position, acceleration}, {acceleration, missing});
  EXPECT_EQ(mapping, (std::vector<int>{1, -1}));
  EXPECT_EQ(
    model::fillVectorUsingMap({0.0, 7.0}, {2.0, 3.0}, mapping, true),
    (std::vector<double>{3.0, 7.0}));
}

TEST(LearnedVehicleModel, EmbeddedPythonSubmodelRoundTrip)
{
  model::InterconnectedModel vehicle;
  pybind11::exec(R"(
class Fixture:
    def __init__(self):
        self.dt = 0.0
    def load_params(self, path):
        assert path == 'declared-fixture'
    def reset(self):
        self.dt = 0.0
    def get_state_names(self):
        return ['position']
    def get_action_names(self):
        return ['velocity']
    def dtSet(self, dt):
        self.dt = dt
    def forward(self, action, state):
        return (state[0] + action[0] * self.dt,)
)");
  vehicle.addSubmodel({"__main__", "declared-fixture", "Fixture"});
  char velocity[] = "velocity";
  char position[] = "position";
  vehicle.generateConnections({velocity}, {position});
  vehicle.dtSet(0.25);
  vehicle.initState({2.0});
  EXPECT_EQ(vehicle.updatePyModel({4.0}), (std::vector<double>{3.0}));
  EXPECT_EQ(vehicle.updatePyModel({4.0}), (std::vector<double>{4.0}));
  vehicle.initState({10.0});
  EXPECT_EQ(vehicle.updatePyModel({-2.0}), (std::vector<double>{9.5}));
}
