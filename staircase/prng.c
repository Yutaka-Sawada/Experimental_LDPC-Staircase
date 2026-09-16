/*
Park-Miler "minimal standard" PRNG

Ij + 1 = A * Ij (modulo M), with the following choices:
A = 7^^5 = 16807 and M = 2^^31 - 1 = 2147483647.

seed = 1 ~ 0x7FFFFFFE
*/
#include <stdint.h>

#include "prng.h"

// Returns 31-bit value between 1 and 0x7FFFFFFE inclusive.
void pmms_next(unsigned int *state)
{
	uint64_t raw_value = *state;

	raw_value = (16807 * raw_value) % 2147483647;
	*state = (unsigned int)raw_value;
}

// Returns random integer between 0 and maxv-1 inclusive.
unsigned int pmms_rand(unsigned int *state, unsigned int maxv)
{
	uint64_t raw_value = *state;	// use 64-bit integer to avoid overflow

	raw_value = (16807 * raw_value) % 2147483647;
	*state = (unsigned int)raw_value;

	return (unsigned int) ((double)maxv * (double)raw_value / (double)0x7FFFFFFF);
}

