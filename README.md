This program simulates a 5-stage RISC-V pipeline (Instruction Fetch → Instruction Decode → Execute → Memory → Write Back) for a sequence of instructions containing only add, sub, and mov.

It models pipeline stalls due to data hazards under a no-forwarding assumption:

If an instruction directly depends on the result of the previous instruction (i-1), the pipeline inserts 2 stall cycles.

If an instruction depends on the result of the second previous instruction (i-2), the pipeline inserts 1 stall cycle.

Otherwise, the instruction issues without delay.

The simulator:

Reads instructions from a text file (instructions.txt).

Computes the execution timeline (cycle numbers for IF/ID/EX/MEM/WB for each instruction).

Outputs the result as both console statistics (total stalls, total cycles) and a CSV file (pipeline_timeline.csv) for detailed analysis.

Includes a manually annotated instruction file (instructions_with_analysis.txt) to cross-check the simulator’s results.
