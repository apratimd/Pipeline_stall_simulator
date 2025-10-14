#include <stdio.h>   // include stdio for file I/O and printf
#include <stdlib.h>  // include stdlib for malloc/free and exit codes
#include <string.h>  // include string.h for strncpy, strcmp, strlen, etc.
#include <ctype.h>   // include ctype.h for isalpha, isdigit, isspace functions

#define MAX_INSTR  4096  // maximum number of instructions supported
#define MAX_LINE   4096  // maximum length of a single input line

typedef enum { OP_ADD, OP_SUB, OP_MOV, OP_BAD } Op;  // opcode enum for our small ISA
typedef struct {  // instruction structure to hold parsed instruction information
    Op   op;               // opcode of the instruction (add/sub/mov)
    char rd[16];           // destination register name string (e.g., "x1")
    char rs[2][16];        // up to two source register name strings
    int  rs_count;         // number of source registers (1 for mov, 2 for add/sub)
    char text[128];        // textual representation of the instruction for tracing/CSV
    int  finished;         // runtime flag: set to 1 when instruction completes WB
} Instr;                  // end of Instr struct definition

/* Helper to remove Byte-Order-Mark if present at start of a line (Notepad-safe) */
static void strip_bom_inplace(char *s) {               // function removes UTF-8 BOM from string
    unsigned char *u = (unsigned char*)s;              // treat input as unsigned bytes
    if (u[0]==0xEF && u[1]==0xBB && u[2]==0xBF)       // check BOM sequence 0xEF,0xBB,0xBF
        memmove(s, s+3, strlen(s+3)+1);               // shift string left to drop BOM bytes
}                                                     // end strip_bom_inplace

/* Trim trailing ASCII whitespace characters in-place */
static void rtrim_ascii(char *s) {                     // function trims trailing whitespace
    int n=(int)strlen(s);                              // get current length of string
    while (n>0) {                                      // while there are characters to check
        unsigned char c=(unsigned char)s[n-1];        // read last character as unsigned
        if (c=='\n'||c=='\r'||c=='\t'||c==' '||c=='\f'||c=='\v') // check common ASCII whitespace
            s[--n]='\0';                              // remove character by moving end marker left
        else break;                                   // stop when a non-whitespace is found
    }                                                  // end while
}                                                      // end rtrim_ascii

/* Remove anything after a '#' comment character on the line */
static void strip_comment(char *s) {                    // function removes comments starting with '#'
    char *p = strchr(s, '#');                           // find first occurrence of '#'
    if (p) *p = '\0';                                   // if found, terminate string there
}                                                      // end strip_comment

/* Return true if the string contains only ASCII whitespace */
static int is_blank_ascii(const char *s) {              // function checks if line is blank/whitespace
    for (int i=0; s[i]; ++i)                            // iterate over characters
        if (!isspace((unsigned char)s[i])) return 0;    // return false on first non-space
    return 1;                                          // all characters were whitespace -> true
}                                                      // end is_blank_ascii

/* Find opcode token "add"/"sub"/"mov" ignoring case; return corresponding enum */
static Op find_opcode(const char *s) {                  // scan the line for a known opcode word
    const char *p = s;                                  // pointer used to walk the string
    while (*p) {                                        // loop until end of string
        while (*p && !isalpha((unsigned char)*p)) p++;  // skip non-alpha characters
        if (!*p) break;                                 // if end reached, break out
        const char *q = p;                              // q will walk the alphabetic word
        char w[16]; int j=0;                            // temporary buffer for the word
        while (*q && isalpha((unsigned char)*q) && j<15) // copy up to 15 letters
            w[j++] = (char)tolower((unsigned char)*q), q++;
        w[j]='\0';                                      // null-terminate the word
        if (strcmp(w,"add")==0) return OP_ADD;          // match "add"
        if (strcmp(w,"sub")==0) return OP_SUB;          // match "sub"
        if (strcmp(w,"mov")==0) return OP_MOV;          // match "mov"
        p = q;                                          // continue search after the word
    }                                                   // end while
    return OP_BAD;                                      // no opcode found
}                                                       // end find_opcode

/* Scan next ASCII register token of form x[0-9]+ anywhere in the string.
   Returns pointer after the matched digits, and writes digits into out[].
   Returns NULL if no register found. */
static const char* find_next_reg(const char *p, char out[16]) { // find next register occurrence
    while (*p) {                                        // walk characters until null
        if (*p=='x' || *p=='X') {                      // potential register start detected
            const char *q = p+1;                       // q will scan the digits after 'x'
            int j=0;                                   // digit buffer index
            while (*q && isdigit((unsigned char)*q)) { // collect contiguous digits
                if (j<14) out[j++] = *q;               // store at most 14 digits (safety)
                q++;
            }
            if (j>0) { out[j]='\0'; return q; }        // if digits found, return pointer after them
        }
        p++;                                           // otherwise advance and keep searching
    }
    return NULL;                                        // no register found
}                                                      // end find_next_reg

/* Parse a single text line into an Instr structure in a tolerant way:
   - find opcode anywhere on the line
   - collect registers by scanning x[0-9]+ tokens
   - verify correct register count for each opcode */
static int parse_line(const char *line_in, Instr *ins, int lineno) { // parse one input line
    char buf[MAX_LINE];                                 // local buffer to manipulate the line
    strncpy(buf, line_in, sizeof(buf)-1); buf[sizeof(buf)-1]='\0'; // safe copy + null-terminate

    strip_bom_inplace(buf);                             // remove BOM if present
    strip_comment(buf);                                 // drop trailing comments starting with '#'
    rtrim_ascii(buf);                                   // trim trailing whitespace/newlines
    if (is_blank_ascii(buf)) return 0;                  // skip blank lines silently

    Op op = find_opcode(buf);                           // detect opcode token in the line
    if (op == OP_BAD) {                                 // if no opcode, treat as non-instruction
        return 0;                                       // skip this line quietly
    }

    char regs[3][16]; int rcount=0;                     // temporary storage for up to 3 registers
    const char *p = buf;                                // scanning pointer
    while (rcount < 3) {                                // collect up to three register tokens
        char digits[16];                                // buffer for digits following 'x'
        const char *np = find_next_reg(p, digits);      // search next register from current position
        if (!np) break;                                 // break if none found
        snprintf(regs[rcount], sizeof(regs[rcount]), "x%s", digits); // store as "x<digits>"
        rcount++;                                       // increment number of regs found
        p = np;                                         // advance scanning pointer beyond the match
    }

    if ((op==OP_ADD || op==OP_SUB) && rcount != 3) {    // add/sub require 3 regs: rd, rs1, rs2
        fprintf(stderr, "Parse error on line %d: need 3 regs for %s; got %d  |  line: \"%s\"\n",
                lineno, (op==OP_ADD?"add":"sub"), rcount, buf); // diagnostic message for professor/debugging
        return -1;                                      // error condition
    }
    if (op==OP_MOV && rcount != 2) {                    // mov requires 2 regs: rd, rs
        fprintf(stderr, "Parse error on line %d: need 2 regs for mov; got %d  |  line: \"%s\"\n",
                lineno, rcount, buf);                    // diagnostic message for mov
        return -1;                                      // error condition
    }

    ins->op = op;                                       // set opcode in parsed instruction
    ins->finished = 0;                                  // clear finished flag at parse time
    if (op == OP_MOV) {                                 // handle mov formatting into Instr
        snprintf(ins->rd,  sizeof(ins->rd),  "%s", regs[0]);        // copy destination register
        snprintf(ins->rs[0],sizeof(ins->rs[0]),"%s", regs[1]);      // copy source register
        ins->rs[1][0]='\0';                            // clear unused second source
        ins->rs_count=1;                               // mov has only one source
        snprintf(ins->text,sizeof(ins->text),"mov %s, %s",ins->rd,ins->rs[0]); // human-readable text
    } else {                                            // handle add/sub formatting
        snprintf(ins->rd,  sizeof(ins->rd),  "%s", regs[0]);        // destination
        snprintf(ins->rs[0],sizeof(ins->rs[0]),"%s", regs[1]);      // source 1
        snprintf(ins->rs[1],sizeof(ins->rs[1]),"%s", regs[2]);      // source 2
        ins->rs_count=2;                               // add/sub have two sources
        snprintf(ins->text,sizeof(ins->text),"%s %s, %s, %s",
                 (op==OP_ADD?"add":"sub"),ins->rd,ins->rs[0],ins->rs[1]); // textual representation
    }
    return 1;                                           // parse successful: instruction filled in
}                                                      // end parse_line

/* Utility: check whether a given register string is present in the instruction's sources */
static int reg_in_sources(const char *reg, const Instr *ins) { // check RAW dependence
    if (!reg || !ins) return 0;                        // guard against NULL pointers
    for (int i=0;i<ins->rs_count;i++)                  // iterate over source registers
        if (strcmp(reg, ins->rs[i])==0) return 1;      // return true if a match is found
    return 0;                                          // not found -> false
}                                                      // end reg_in_sources

/* Human-readable trace helpers: these functions print the action in the named stage.
   They do not modify pipeline state; they are only for logging/tracing output. */
static void fetch_trace(int idx, Instr *prog, int cycle) { // log fetch stage activity
    if (idx >= 0) printf("C%3d: FETCH  [%2d] %s\n", cycle, idx, prog[idx].text); // print IF stage
}
static void decode_trace(int idx, Instr *prog, int cycle) { // log decode stage
    if (idx >= 0) printf("C%3d: DECODE [%2d] %s\n", cycle, idx, prog[idx].text); // print ID stage
}
static void execute_trace(int idx, Instr *prog, int cycle) { // log execute stage
    if (idx >= 0) printf("C%3d: EXEC   [%2d] %s\n", cycle, idx, prog[idx].text); // print EX stage
}
static void memory_trace(int idx, Instr *prog, int cycle) {  // log memory stage (bypassed for now)
    if (idx >= 0) printf("C%3d: MEM    [%2d] %s (bypassed)\n", cycle, idx, prog[idx].text); // MEM stage trace
}
static void write_back_action(int idx, Instr *prog, int cycle) { // perform write-back logging
    if (idx >= 0) {                                     // only if there is an instruction in WB
        printf("C%3d: WB     [%2d] %s -> write %s\n", cycle, idx, prog[idx].text, prog[idx].rd); // trace write
        prog[idx].finished = 1;                        // mark instruction as finished (WB completed)
    }
}                                                      // end write_back_action

/* Determine the number of stall bubbles required if an instruction is currently in ID:
   - if the instruction in EX will write a register that ID reads -> need 2 bubbles
   - if the instruction in MEM will write a register that ID reads -> need 1 bubble
   This matches the no-forwarding RAW stall model used earlier. */
static int needed_stalls_for_id(int id_idx, int ex_idx, int mem_idx, Instr *prog) { // hazard detection
    if (id_idx < 0) return 0;                          // nothing in ID -> no stalls needed
    if (ex_idx >= 0 && reg_in_sources(prog[ex_idx].rd, &prog[id_idx])) return 2; // EX producer -> 2 cycles
    if (mem_idx >= 0 && reg_in_sources(prog[mem_idx].rd, &prog[id_idx])) return 1; // MEM producer -> 1 cycle
    return 0;                                          // no RAW hazard -> 0 stalls
}                                                      // end needed_stalls_for_id

int main(int argc, char **argv) {                       // program entry point
    const char *infile = (argc>=2? argv[1] : "instructions.txt"); // input filename or default

    FILE *f = fopen(infile, "r");                       // open the input file for reading
    if (!f) { fprintf(stderr, "Error: cannot open %s\n", infile); return 1; } // error if cannot open

    Instr prog[MAX_INSTR];                              // array to hold parsed instructions
    int n=0, lineno=0;                                  // n = count parsed, lineno = input line number
    char line[MAX_LINE];                                // buffer to read each input line

    while (fgets(line, sizeof(line), f)) {              // read the file line-by-line
        lineno++;                                       // increment line counter for diagnostics
        Instr ins;                                      // temporary Instr to parse into
        int r = parse_line(line, &ins, lineno);         // parse the current line
        if (r < 0) { fclose(f); return 2; }             // parse error -> exit with code 2
        if (r == 1) {                                   // r==1 means an instruction was parsed
            if (n >= MAX_INSTR) { fprintf(stderr, "Too many instructions\n"); fclose(f); return 3; } // overflow guard
            prog[n++] = ins;                            // store parsed instruction into program array
        }
        /* r==0 means blank or non-instruction line -> skip silently */
    }
    fclose(f);                                          // close input file after reading
    if (n == 0) { fprintf(stderr, "No instructions parsed.\n"); return 4; } // nothing to simulate -> exit

    /* pipeline registers: hold indices into prog[] or -1 for bubble/empty */
    int pipe[5] = { -1, -1, -1, -1, -1 };               // mapping: pipe[0]=IF, [1]=ID, [2]=EX, [3]=MEM, [4]=WB
    int pc = 0;                                         // program counter: next instruction index to fetch
    int completed = 0;                                  // count of instructions that finished WB
    long total_stalls = 0;                              // count of bubble cycles inserted overall
    int cycle = 0;                                      // current cycle number (increments each loop)
    int stall_counter = 0;                              // remaining bubble cycles to insert for current hazard

    FILE *csv = fopen("pipeline_cycles.csv", "w");      // open CSV output for writing
    if (!csv) { fprintf(stderr, "Error: cannot write pipeline_cycles.csv\n"); return 5; } // error if cannot open
    fprintf(csv, "cycle,IF,ID,EX,MEM,WB,stalls_pending\n"); // CSV header row describing columns

    printf("Starting cycle-by-cycle simulation (no-forwarding model)\n"); // human-readable header
    printf("Total instructions: %d\n\n", n);             // print number of instructions parsed

    while (completed < n) {                             // run until all instructions complete WB
        cycle++;                                       // advance to next cycle number

        /* --- Handle write-back for instruction already in WB at start of this cycle --- */
        if (pipe[4] >= 0) {                             // if there is an instruction in WB slot
            write_back_action(pipe[4], prog, cycle);   // perform write-back action/logging
            completed++;                               // increment completed count
            pipe[4] = -1;                              // clear WB slot after write-back
        }

        /* --- If we are currently inserting stalls (stall_counter > 0) then:
              - advance MEM->WB and EX->MEM
              - place a bubble in EX (pipe[2] = -1)
              - keep ID and IF unchanged (they are stalled)
              This models inserting a NOP into EX for each stall cycle. */
        if (stall_counter > 0) {                        // check if bubble cycles remain to be inserted
            if (pipe[3] >= 0) { pipe[4] = pipe[3]; pipe[3] = -1; } // MEM -> WB move
            if (pipe[2] >= 0) { pipe[3] = pipe[2]; pipe[2] = -1; } // EX -> MEM move, EX becomes bubble
            /* ID and IF remain in their slots (stalled) */
            total_stalls++;                             // count this bubble cycle in totals
            stall_counter--;                            // one less stall to insert

            /* produce human-readable traces for stages that have content this cycle */
            if (pipe[3] >= 0) memory_trace(pipe[3], prog, cycle); // trace MEM stage if occupied
            if (pipe[2] >= 0) execute_trace(pipe[2], prog, cycle); // typically -1 here
            if (pipe[1] >= 0) decode_trace(pipe[1], prog, cycle); // ID is stalled -> show it
            if (pipe[0] >= 0) fetch_trace(pipe[0], prog, cycle);  // IF is stalled -> show it

            /* write a CSV snapshot of the pipeline after this cycle's movement */
            char buf_if[256] = "", buf_id[256] = "", buf_ex[256] = "", buf_mem[256] = "", buf_wb[256] = "";
            if (pipe[0] >= 0) snprintf(buf_if, sizeof(buf_if), "\"%s\"", prog[pipe[0]].text); // quote text
            if (pipe[1] >= 0) snprintf(buf_id, sizeof(buf_id), "\"%s\"", prog[pipe[1]].text);
            if (pipe[2] >= 0) snprintf(buf_ex, sizeof(buf_ex), "\"%s\"", prog[pipe[2]].text);
            if (pipe[3] >= 0) snprintf(buf_mem, sizeof(buf_mem), "\"%s\"", prog[pipe[3]].text);
            if (pipe[4] >= 0) snprintf(buf_wb, sizeof(buf_wb), "\"%s\"", prog[pipe[4]].text);
            fprintf(csv, "%d,%s,%s,%s,%s,%s,%d\n", cycle, // write CSV row: cycle and stage contents
                    buf_if, buf_id, buf_ex, buf_mem, buf_wb, stall_counter);
            continue;                                   // move to next cycle iteration
        }

        /* --- Normal advancement when no stall insertion is required:
              advance pipeline right-to-left: MEM->WB, EX->MEM, ID->EX, IF->ID, then fetch a new IF. */

        if (pipe[3] >= 0) { pipe[4] = pipe[3]; pipe[3] = -1; } // move MEM to WB
        if (pipe[2] >= 0) { pipe[3] = pipe[2]; pipe[2] = -1; } // move EX to MEM
        if (pipe[1] >= 0) { pipe[2] = pipe[1]; pipe[1] = -1; } // move ID to EX
        if (pipe[0] >= 0) { pipe[1] = pipe[0]; pipe[0] = -1; } // move IF to ID
        if (pc < n) { pipe[0] = pc++; } else pipe[0] = -1;      // fetch new instruction into IF if available

        /* After movement, detect RAW hazards for instruction now in ID and set stall_counter if needed.
           Producers we consider are the instructions currently in EX and MEM (after movement above). */
        {
            int id_idx = pipe[1];                       // instruction index now in ID
            int ex_idx = pipe[2];                       // instruction index now in EX
            int mem_idx = pipe[3];                      // instruction index now in MEM
            int req = needed_stalls_for_id(id_idx, ex_idx, mem_idx, prog); // determine required stalls
            if (req > 0) {                              // if stalls needed
                if (pipe[2] == id_idx) {                // if we moved ID->EX earlier and must undo
                    pipe[1] = pipe[2];                  // move it back to ID so ID stays
                    pipe[2] = -1;                       // set EX to bubble
                }
                stall_counter = req;                    // set stall counter so bubbles will be inserted next cycles
                /* Note: actual bubble insertion happens at the top of the next iterations */
            }
        }

        /* Print human-readable trace for this cycle after the movement.
           Note: WB content already handled at the top of the loop this cycle. */
        if (pipe[4] >= 0) {                               // if something is now in WB (pending write next cycle)
            printf("C%3d: WB-pend [%2d] %s (will write next cycle)\n", cycle, pipe[4], prog[pipe[4]].text);
        }
        if (pipe[3] >= 0) memory_trace(pipe[3], prog, cycle); // trace MEM stage
        if (pipe[2] >= 0) execute_trace(pipe[2], prog, cycle); // trace EX stage
        if (pipe[1] >= 0) decode_trace(pipe[1], prog, cycle); // trace ID stage
        if (pipe[0] >= 0) fetch_trace(pipe[0], prog, cycle);  // trace IF stage

        /* write CSV snapshot for this cycle after movement */
        char buf_if[256] = "", buf_id[256] = "", buf_ex[256] = "", buf_mem[256] = "", buf_wb[256] = "";
        if (pipe[0] >= 0) snprintf(buf_if, sizeof(buf_if), "\"%s\"", prog[pipe[0]].text); // quote instruction text
        if (pipe[1] >= 0) snprintf(buf_id, sizeof(buf_id), "\"%s\"", prog[pipe[1]].text);
        if (pipe[2] >= 0) snprintf(buf_ex, sizeof(buf_ex), "\"%s\"", prog[pipe[2]].text);
        if (pipe[3] >= 0) snprintf(buf_mem, sizeof(buf_mem), "\"%s\"", prog[pipe[3]].text);
        if (pipe[4] >= 0) snprintf(buf_wb, sizeof(buf_wb), "\"%s\"", prog[pipe[4]].text);
        fprintf(csv, "%d,%s,%s,%s,%s,%s,%d\n", cycle, // CSV row: pipeline contents and stalls_pending
                buf_if, buf_id, buf_ex, buf_mem, buf_wb, stall_counter);
    }                                                   // end while (simulation loop)

    fclose(csv);                                        // close CSV file now that simulation completed

    printf("\nSimulation finished in %d cycles.\n", cycle); // print summary: total cycles
    printf("Total stalls (bubble cycles inserted): %ld\n", total_stalls); // total bubble count
    printf("Base cycles (N+4): %d\n", n + 4);            // theoretical base cycles without hazards
    printf("Total cycles with stalls: %d\n", cycle);    // actual cycles used by simulation
    printf("CSV written to pipeline_cycles.csv\n");     // indicate location of CSV output

    return 0;                                           // normal program exit
}                                                      // end main and end of file
