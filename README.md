# dandelion
HiMCM 2023 Problem A submission

Simulates dandelion spread and growth

100x100 grid

Four worker threads that each simulate their own quadrant of the grid

Worker threads send back new seeds to be spread to master thread using queues

Uses raylib for visualization
