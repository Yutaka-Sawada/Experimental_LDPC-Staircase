#ifndef LDPC_BITMASK_H
#define LDPC_BITMASK_H

#include <stdint.h>

#define IDXBITS 64

uint32_t bitmask_len(uint32_t max_len);

void bitmask_set(uint64_t *bm, uint32_t id);
void bitmask_clear(uint64_t *bm, uint32_t id);
uint32_t bitmask_check(uint64_t *bm, uint32_t id);

void bitmask_reset(uint64_t *bm, uint32_t max_len);
void bitmask_fill(uint64_t *bm, uint32_t until_pos);

uint32_t bitmask_pop(uint64_t *bm, uint32_t idx_max);
uint32_t bitmask_ntz(uint64_t *bm, uint32_t idx_max);

void print_bit_matrix(
	uint64_t *matrix,
	unsigned int num_col,	// number of columns = size of row
	unsigned int num_row);	// number of rows = size of column

#endif
