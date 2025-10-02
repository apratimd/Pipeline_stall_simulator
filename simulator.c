// pipeline_sim.c  — minimal tolerant parser for Notepad files
// 5-stage pipeline (IF, ID, EX, MEM, WB), ALU-only, NO FORWARDING.
// RAW stalls: i-1 => +2, i-2 => +1 (max).
// Build: gcc -std=c99 -O2 pipeline_sim.c -o pipeline_sim
// Run:   ./pipeline_sim [instructions.txt] [pipeline_timeline.csv]

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_INSTR  4096
#define MAX_LINE   4096

typedef enum { OP_ADD, OP_SUB, OP_MOV, OP_BAD } Op;
typedef struct {
    Op   op;
    char rd[16];
    char rs[2][16];
    int  rs_count;
    char text[128];
} Instr;

static void strip_bom_inplace(char *s) {
    unsigned char *u = (unsigned char*)s;
    if (u[0]==0xEF && u[1]==0xBB && u[2]==0xBF) memmove(s, s+3, strlen(s+3)+1);
}
static void rtrim_ascii(char *s) {
    int n=(int)strlen(s);
    while (n>0) {
        unsigned char c=(unsigned char)s[n-1];
        if (c=='\n'||c=='\r'||c=='\t'||c==' '||c=='\f'||c=='\v') s[--n]='\0'; else break;
    }
}
static void strip_comment(char *s) {
    char *p = strchr(s, '#');
    if (p) *p = '\0';
}
static int is_blank_ascii(const char *s) {
    for (int i=0; s[i]; ++i) if (!isspace((unsigned char)s[i])) return 0;
    return 1;
}

/* find op token "add"/"sub"/"mov" ignoring case; returns enum or OP_BAD */
static Op find_opcode(const char *s) {
    const char *p = s;
    while (*p) {
        while (*p && !isalpha((unsigned char)*p)) p++;
        if (!*p) break;
        const char *q = p;
        char w[16]; int j=0;
        while (*q && isalpha((unsigned char)*q) && j<15) w[j++] = (char)tolower((unsigned char)*q), q++;
        w[j]='\0';
        if (strcmp(w,"add")==0) return OP_ADD;
        if (strcmp(w,"sub")==0) return OP_SUB;
        if (strcmp(w,"mov")==0) return OP_MOV;
        p = q;
    }
    return OP_BAD;
}

/* scan next ASCII register x[0-9]+ anywhere; returns pointer after match, or NULL if none */
static const char* find_next_reg(const char *p, char out[16]) {
    while (*p) {
        if (*p=='x' || *p=='X') {
            const char *q = p+1;
            int j=0;
            while (*q && isdigit((unsigned char)*q)) {
                if (j<14) out[j++] = *q;
                q++;
            }
            if (j>0) { out[j]='\0'; return q; }
        }
        p++;
    }
    return NULL;
}

/* tolerant line parser: finds opcode, then collects registers by scanning x[0-9]+ */
static int parse_line(const char *line_in, Instr *ins, int lineno) {
    char buf[MAX_LINE];
    strncpy(buf, line_in, sizeof(buf)-1); buf[sizeof(buf)-1]='\0';

    strip_bom_inplace(buf);            // handle BOM on every line (Notepad-safe)
    strip_comment(buf);                // allow trailing comments
    rtrim_ascii(buf);
    if (is_blank_ascii(buf)) return 0; // skip empty/whitespace

    Op op = find_opcode(buf);
    if (op == OP_BAD) {
        // not an instruction, skip quietly
        return 0;
    }

    char regs[3][16]; int rcount=0;
    const char *p = buf;
    while (rcount < 3) {
        char digits[16];
        const char *np = find_next_reg(p, digits);
        if (!np) break;
        snprintf(regs[rcount], sizeof(regs[rcount]), "x%s", digits);
        rcount++;
        p = np;
    }

    if ((op==OP_ADD || op==OP_SUB) && rcount != 3) {
        fprintf(stderr, "Parse error on line %d: need 3 regs for %s; got %d  |  line: \"%s\"\n",
                lineno, (op==OP_ADD?"add":"sub"), rcount, buf);
        return -1;
    }
    if (op==OP_MOV && rcount != 2) {
        fprintf(stderr, "Parse error on line %d: need 2 regs for mov; got %d  |  line: \"%s\"\n",
                lineno, rcount, buf);
        return -1;
    }

    ins->op = op;
    if (op == OP_MOV) {
        snprintf(ins->rd,  sizeof(ins->rd),  "%s", regs[0]);
        snprintf(ins->rs[0],sizeof(ins->rs[0]),"%s", regs[1]);
        ins->rs[1][0]='\0';
        ins->rs_count=1;
        snprintf(ins->text,sizeof(ins->text),"mov %s, %s",ins->rd,ins->rs[0]);
    } else {
        snprintf(ins->rd,  sizeof(ins->rd),  "%s", regs[0]);
        snprintf(ins->rs[0],sizeof(ins->rs[0]),"%s", regs[1]);
        snprintf(ins->rs[1],sizeof(ins->rs[1]),"%s", regs[2]);
        ins->rs_count=2;
        snprintf(ins->text,sizeof(ins->text),"%s %s, %s, %s",
                 (op==OP_ADD?"add":"sub"),ins->rd,ins->rs[0],ins->rs[1]);
    }
    return 1;
}

static int reg_in_sources(const char *reg, const Instr *ins) {
    for (int i=0;i<ins->rs_count;i++) if (strcmp(reg, ins->rs[i])==0) return 1;
    return 0;
}

int main(int argc, char **argv) {
    const char *infile = (argc>=2? argv[1] : "instructions.txt");
    const char *csvout = (argc>=3? argv[2] : "pipeline_timeline.csv");

    FILE *f = fopen(infile, "r");
    if (!f) { fprintf(stderr, "Error: cannot open %s\n", infile); return 1; }

    Instr prog[MAX_INSTR];
    int n=0, lineno=0;
    char line[MAX_LINE];

    while (fgets(line, sizeof(line), f)) {
        lineno++;
        Instr ins;
        int r = parse_line(line, &ins, lineno);
        if (r < 0) { fclose(f); return 2; }
        if (r == 1) {
            if (n >= MAX_INSTR) { fprintf(stderr, "Too many instructions\n"); fclose(f); return 3; }
            prog[n++] = ins;
        }
    }
    fclose(f);
    if (n == 0) { fprintf(stderr, "No instructions parsed.\n"); return 4; }

    // No-forwarding hazard model
    int *stalls = (int*)calloc(n, sizeof(int));
    int *IFc  = (int*)malloc(n*sizeof(int));
    int *IDc  = (int*)malloc(n*sizeof(int));
    int *EXc  = (int*)malloc(n*sizeof(int));
    int *MEMc = (int*)malloc(n*sizeof(int));
    int *WBc  = (int*)malloc(n*sizeof(int));
    if (!stalls || !IFc || !IDc || !EXc || !MEMc || !WBc) { fprintf(stderr, "OOM\n"); return 5; }

    char d1[16]="", d2[16]="";
    for (int i=0;i<n;i++) {
        int s=0;
        if (d1[0] && reg_in_sources(d1, &prog[i])) s=2;
        if (d2[0] && reg_in_sources(d2, &prog[i])) s = (s>1? s:1);
        stalls[i]=s;
        snprintf(d2,sizeof(d2),"%s",d1);
        snprintf(d1,sizeof(d1),"%s",prog[i].rd);
    }

    int curIF=1;
    for (int i=0;i<n;i++) {
        int s=stalls[i];
        int start_if=curIF+s;
        IFc[i]=start_if; IDc[i]=start_if+1; EXc[i]=start_if+2; MEMc[i]=start_if+3; WBc[i]=start_if+4;
        curIF = start_if + 1;
    }

    long sum_stalls=0; for (int i=0;i<n;i++) sum_stalls+=stalls[i];
    int base_cycles = n + 4;
    int total_cycles = WBc[n-1];

    printf("Instructions: %d\n", n);
    printf("Base cycles (N+4): %d\n", base_cycles);
    printf("Total stalls: %ld\n", sum_stalls);
    printf("Total cycles with stalls: %d\n", total_cycles);
    printf("Per-instruction stalls (index:stalls):\n");
    for (int i=0;i<n;i++) printf("%d:%d%s", i, stalls[i], (i==n-1) ? "\n" : ", ");

    FILE *csv = fopen(csvout, "w");
    if (!csv) { fprintf(stderr, "Error: cannot write %s\n", csvout); return 6; }
    fprintf(csv, "idx,instruction,IF,ID,EX,MEM,WB,stalls_here\n");
    for (int i=0;i<n;i++)
        fprintf(csv, "%d,%s,%d,%d,%d,%d,%d,%d\n",
                i, prog[i].text, IFc[i], IDc[i], EXc[i], MEMc[i], WBc[i], stalls[i]);
    fclose(csv);

    free(stalls); free(IFc); free(IDc); free(EXc); free(MEMc); free(WBc);
    return 0;
}
