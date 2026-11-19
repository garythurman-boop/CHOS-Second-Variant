#ifndef CHOS_DIGEST_FAULTS_H
#define CHOS_DIGEST_FAULTS_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void digest_arm_fault(unsigned);
unsigned digest_fault_hits(void);
int digest_backend_tests(void);
unsigned digest_fault_id(unsigned algorithm,unsigned operation);
void digest_case_input(unsigned kind,size_t offset,uint8_t *out,size_t length);
#ifdef __cplusplus
}
#endif
#endif
