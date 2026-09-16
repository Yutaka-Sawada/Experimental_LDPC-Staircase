#ifndef LDPC_PRNG_H
#define LDPC_PRNG_H

void pmms_next(unsigned int *state);
unsigned int pmms_rand(unsigned int *state, unsigned int maxv);

#endif
