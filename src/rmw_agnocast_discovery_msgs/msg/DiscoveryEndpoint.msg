# Unique endpoint profile used for matching volatile topics.
string topic_name
string type_name
rosgraph_msgs/TypeHash type_hash

uint8 PUBLISHER=1
uint8 SUBSCRIPTION=2
uint8 role

uint8 RELIABLE=1
uint8 BEST_EFFORT=2
uint8 reliability
