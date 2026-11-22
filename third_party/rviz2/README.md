# RViz runtime adapter

`runtime` supplies the registry RViz executable at `lib/rviz2/rviz2`, with its
original SDK indexes and binary runfiles. It uses `register_ament_files` rather
than the SDK's CMake-oriented `ament_package` closure. The root selects the RViz
release; this adapter owns no Autoware plugin or product configuration.

Products compose their own plugin indexes and native graphics runtime. A desktop
provides authenticated display access and a compatible rendering driver.
