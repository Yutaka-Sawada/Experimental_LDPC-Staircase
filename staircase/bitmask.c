/*
MIT License

Copyright (c) 2026 Yutaka Sawada (modifier for subset)
Copyright (c) 2020 Joseph Calderon (original author of nanorq)

I removed __builtin_popcount, because old CPU may not support popcount.
*/
#include <stdio.h>

#include "bitmask.h"


// returns number of integers at allocating memory
uint32_t bitmask_len(uint32_t max_len) {
	return (max_len + IDXBITS - 1) / IDXBITS;	// number of 64-bit integers
}

// 0 <= id < max_len
void bitmask_set(uint64_t *bm, uint32_t id) {
	uint32_t idx = id / IDXBITS;
	uint64_t add_mask = 1ull << (id % IDXBITS);
	bm[idx] |= add_mask;
}

void bitmask_clear(uint64_t *bm, uint32_t id) {
	uint32_t idx = id / IDXBITS;
	uint64_t add_mask = 1ull << (id % IDXBITS);
	bm[idx] &= ~add_mask;
}

uint32_t bitmask_check(uint64_t *bm, uint32_t id) {
	uint32_t idx = id / IDXBITS;
	uint64_t check_bit = bm[idx] >> (id % IDXBITS);
	return (uint32_t)(check_bit & 1);
}

/*
https://en.wikipedia.org/wiki/Hamming_weight

This uses fewer arithmetic operations than any other known  
implementation on machines with slow multiplication.
This algorithm uses 17 arithmetic operations.
popcount64b
*/
#define M1 0x5555555555555555	//binary: 0101...
#define M2 0x3333333333333333	//binary: 00110011..
#define M4 0x0f0f0f0f0f0f0f0f	//binary:  4 zeros,  4 ones ...

/*
looping method is slow.
	for (int i = 0; i < IDXBITS; i++){
		pops += x & 1;
		x >>= 1;
	}
*/

/*
// returns number of bit 1 until until_pos
// until_pos must be equal or less than max_len
uint32_t bitmask_pop(uint64_t *bm, uint32_t until_pos) {
	uint32_t idx, pops = 0;
	uint32_t until_idx = until_pos / IDXBITS;
	uint64_t x, until_mask;

	for (idx = 0; idx < until_idx; idx++) {
		x = bm[idx];
		// popcount64b
		x -= (x >> 1) & M1;				//put count of each 2 bits into those 2 bits
		x = (x & M2) + ((x >> 2) & M2);	//put count of each 4 bits into those 4 bits
		x = (x + (x >> 4)) & M4;		//put count of each 8 bits into those 8 bits
		x += x >>  8;					//put count of each 16 bits into their lowest 8 bits
		x += x >> 16;					//put count of each 32 bits into their lowest 8 bits
		x += x >> 32;					//put count of each 64 bits into their lowest 8 bits
		pops += x & 0x7f;
	}
	if (until_pos % IDXBITS) {
		until_mask = (1ull << (until_pos % IDXBITS)) - 1;
		x = bm[idx] & until_mask;
		// popcount64b
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
*/

// returns number of bit 1
uint32_t bitmask_pop(
	uint64_t *bm,
	uint32_t idx_max)	// number of integers
{
	uint32_t idx, pops = 0;
	uint64_t x;

	for (idx = 0; idx < idx_max; idx++) {
		x = bm[idx];

		// popcount64b
		x -= (x >> 1) & M1;				//put count of each 2 bits into those 2 bits
		x = (x & M2) + ((x >> 2) & M2);	//put count of each 4 bits into those 4 bits
		x = (x + (x >> 4)) & M4;		//put count of each 8 bits into those 8 bits
		x += x >>  8;					//put count of each 16 bits into their lowest 8 bits
		x += x >> 16;					//put count of each 32 bits into their lowest 8 bits
		x += x >> 32;					//put count of each 64 bits into their lowest 8 bits
		pops += x & 0x7f;
	}

	return pops;
}

// Number of Training Zero (NTZ)
// returns the number of zeros until the first bit 1
uint32_t bitmask_ntz(
	uint64_t *bm,
	uint32_t idx_max)	// number of integers
{
	uint32_t idx, pops;
	uint64_t x;

	for (idx = 0; idx < idx_max; idx++){
		if (bm[idx] != 0){
			x = bm[idx];
			x = (x & (-(int64_t)x)) - 1;

			// popcount64b
			x -= (x >> 1) & M1;
			x = (x & M2) + ((x >> 2) & M2);
			x = (x + (x >> 4)) & M4;
			x += x >>  8;
			x += x >> 16;
			x += x >> 32;
			pops = x & 0x7f;

			return idx * IDXBITS + pops;
		}
	}

	return idx_max * IDXBITS;	// all zeros
}


// zero fill until max_len
void bitmask_reset(uint64_t *bm, uint32_t max_len) {
	uint32_t idx;
	uint32_t idx_max = (max_len + IDXBITS - 1) / IDXBITS;

	for (idx = 0; idx < idx_max; idx++)
		bm[idx] = 0;
}

// fill bits until until_pos
// until_pos must be equal or less than max_len
void bitmask_fill(uint64_t *bm, uint32_t until_pos) {
	uint32_t idx;
	uint32_t until_idx = until_pos / IDXBITS;
	uint64_t until_mask;

	for (idx = 0; idx < until_idx; idx++) {
		bm[idx] = ~1ull;
	}
	if (until_pos % IDXBITS) {
		until_mask = (1ull << (until_pos % IDXBITS)) - 1;
		bm[idx] |= until_mask;
	}
}


void print_bit_matrix(
	uint64_t *matrix,
	unsigned int num_col,	// number of columns = size of row
	unsigned int num_row)	// number of rows = size of column
{
	unsigned int col_id, row_id;
	unsigned int int_col = (num_col + IDXBITS - 1) / IDXBITS;	// number of integers for columns
	uint64_t *row_p = matrix;

	printf("matrix (%u integers) %u * %u\n", int_col, num_col, num_row);
	for (row_id = 0; row_id < num_row; row_id++){
		for (col_id = 0; col_id < num_col; col_id++){
			printf(" %u", bitmask_check(row_p, col_id));
		}
		row_p += int_col;
		printf("\n");
	}
}
