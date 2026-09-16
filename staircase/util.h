#ifndef LDPC_UTILITY_H
#define LDPC_UTILITY_H

void align_xor(unsigned char *dst, unsigned char *src, unsigned int len);

unsigned int align_xor_pop(uint64_t *dst, uint64_t *src, unsigned int cnt);

#endif
