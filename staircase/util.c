/*
MIT License
Copyright (c) 2026 Yutaka Sawada
*/
#include <stdint.h>

// Using xmm register for XOR on SSE2 is almoast same speed as 64-bit register.


// Both dst and src must be aligned.
void align_xor(unsigned char *dst, unsigned char *src, unsigned int len)
{
	len /= 8;	// convert from bytes to number of uint64_t.
	for (unsigned int i = 0; i < len; i++)
		((uint64_t *)dst)[i] ^= ((uint64_t *)src)[i];
}

// XOR and popcount
#define M1 0x5555555555555555
#define M2 0x3333333333333333
#define M4 0x0f0f0f0f0f0f0f0f
unsigned int align_xor_pop(
	uint64_t *dst, uint64_t *src,
	unsigned int cnt)	// number of uint64_t
{
	uint32_t i, pops = 0;
	uint64_t x;

	for (i = 0; i < cnt; i++){
		x = dst[i] ^ src[i];
		dst[i] = x;

		// popcount64b from https://en.wikipedia.org/wiki/Hamming_weight
		x -= (x >> 1) & M1;
		x = (x & M2) + ((x >> 2) & M2);
		x = (x + (x >> 4)) & M4;
		x += x >>  8;
		x += x >> 16;
		x += x >> 32;
		pops += x & 0x7f;
	}

	return pops;
}

