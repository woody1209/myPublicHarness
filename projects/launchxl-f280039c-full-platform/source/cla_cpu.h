#ifndef FULLPLATFORM_CLA_CPU_H
#define FULLPLATFORM_CLA_CPU_H
#include <stdint.h>
#include <stdbool.h>
bool cla_cpu_init(void);
void cla_cpu_step(void);
void cla_cpu_judge(void);
extern volatile uint32_t g_cla_ok;
extern volatile uint32_t g_cla_err;
extern volatile uint32_t g_cla_runs;
#endif
