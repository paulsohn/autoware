# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Exercise the original Python wrapper through its native CDR binding."""
import subprocess
import sys
import unittest

import autoware_freespace_planning_algorithms.astar_search as fp
from geometry_msgs.msg import Pose
from nav_msgs.msg import OccupancyGrid

DEBUG_PLOT = sys.argv.pop(1)


class TestBindings(unittest.TestCase):
    def test_astar_cdr_round_trip(self):
        params = fp.PlannerCommonParam()
        for key, value in {'time_limit': 10000.0, 'theta_size': 72, 'max_turning_ratio': 0.5,
                           'turning_steps': 1, 'curve_weight': 1.2, 'reverse_weight': 1.0,
                           'direction_change_weight': 2.0, 'lateral_goal_range': 0.5,
                           'longitudinal_goal_range': 0.5, 'angle_goal_range': 10.0,
                           'obstacle_threshold': 100}.items():
            setattr(params, key, value)
        astar_params = fp.AstarParam()
        for key, value in {'search_method': 'forward', 'only_behind_solutions': False,
                           'use_back': True, 'adapt_expansion_distance': True, 'expansion_distance': 0.4,
                           'near_goal_distance': 3.0, 'distance_heuristic_weight': 1.0,
                           'smoothness_weight': 1.0, 'obstacle_distance_weight': 1.0,
                           'goal_lat_distance_weight': 1.0}.items():
            setattr(astar_params, key, value)
        planner = fp.AstarSearch(params, fp.VehicleShape(2.0, 1.0, 1.2, 0.6, 0.5), astar_params)
        grid = OccupancyGrid()
        grid.header.frame_id = 'map'
        grid.info.resolution = 0.2
        grid.info.width = grid.info.height = 100
        grid.info.origin.position.x = grid.info.origin.position.y = -10.0
        grid.info.origin.orientation.w = 1.0
        grid.data = [0] * 10000
        planner.setMap(grid)
        start, goal = Pose(), Pose()
        start.position.x, goal.position.x = -3.0, 3.0
        start.orientation.w = goal.orientation.w = 1.0
        self.assertTrue(planner.makePlan(start, goal))
        waypoints = planner.getWaypoints()
        self.assertGreater(waypoints.compute_length(), 5.0)
        self.assertGreater(len(waypoints.waypoints), 2)
        self.assertAlmostEqual(waypoints.waypoints[0].pose.position.x, -3.0, delta=0.3)
        self.assertAlmostEqual(waypoints.waypoints[-1].pose.position.x, 3.0, delta=0.5)
        self.assertGreater(planner.getDistanceToObstacle(start), 0.5)

    def test_debug_tool_imports(self):
        result = subprocess.run([DEBUG_PLOT, '--help'], capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('usage:', result.stdout)


if __name__ == '__main__':
    unittest.main()
