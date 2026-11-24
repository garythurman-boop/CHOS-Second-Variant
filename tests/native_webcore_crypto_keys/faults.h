#ifndef CHOS_KEY_FAULTS_H
#define CHOS_KEY_FAULTS_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void key_fault_begin(size_t);
size_t key_fault_end(void);
int key_fault_injected(void);
size_t key_fault_live(void);
#ifdef __cplusplus
}
#endif
#endif
