#!/bin/bash

# Script must be run while working directory is ~/LabG3 and run using ./grep.sh (or whatever you name it)
# don't forget to chmod +x grep.sh

benchmarks=("gcc" "mcf" "milc" "omnetpp")

# run all benchmarks using run_gem5
# change executable to run_gem5 or run_gem5_log
# change argument to ooo-default.cfg or ooo-mine.cfg

for bench in ${benchmarks[@]}; do
    echo Running on ${bench}
    # ramulator/ramulator ramulator/configs/baseline.cfg --mode=cpu --stats output/${bench}.stats traces/${bench}.trace

    grep "ramulator.record_cycs_core_0" output/${bench}.stats
done

echo "Getting multiprogrammed info"
for i in {0..3}; do
    grep "ramulator.record_cycs_core_${i}" output/baseline.stats
done
