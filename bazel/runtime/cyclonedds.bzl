"""Discovery capacity for the full single-host planning simulator profile."""

# RMW defaults to indices 0..32; the original simulator starts more DDS processes.
# 119 keeps auto-selected unicast ports inside the standard per-domain range.
PLANNING_SIMULATOR_CYCLONEDDS_URI = "<CycloneDDS><Domain><Discovery><MaxAutoParticipantIndex>119</MaxAutoParticipantIndex></Discovery></Domain></CycloneDDS>"
