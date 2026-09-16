// LDPC-Staircase
// Copyright (c) 2026 Yutaka Sawada
// MIT License

#include <stdio.h>
#include <stdlib.h>

#include "staircase.h"
#include "bipart.h"
#include "bitmask.h"
#include "matrix.h"
#include "prng.h"
#include "solve.h"
#include "util.h"


ldpc_coder *ldpc_encoder_new(
	unsigned int num_source,	// number of source symbols
	unsigned int num_repair,	// number of repair symbols
	unsigned int symbol_size)	// size of each source symbol in bytes
{
	// Becasue row weight >= 2, source symbols >= 3.
	if ((num_source < 3) || (num_repair < 1) || (symbol_size < 1))
		return NULL;

	ldpc_coder *ldpc = calloc(1, sizeof(ldpc_coder));
	if (ldpc == NULL)
		return NULL;

	// Common Elements
	ldpc->num_src = num_source;
	ldpc->num_rep = num_repair;
	ldpc->sym_size = symbol_size;
	ldpc->al_size = (symbol_size + SYMBOL_ALIGN - 1) & ~(SYMBOL_ALIGN - 1);
	//printf("al_size = %u\n", ldpc->al_size);

	// allocate buffer for working space
	if (allocate_buffer(ldpc) != 0){
		ldpc_free(ldpc);
		return NULL;
	}

	// construct matrix
	if (create_matrix(ldpc) != 0){
		ldpc_free(ldpc);
		return NULL;
	}

	// setup list of source symbols
	ldpc->id_mask = calloc(bitmask_len(num_source), sizeof(uint64_t));	// zero fill
	if (ldpc->id_mask == NULL){
		ldpc_free(ldpc);
		return NULL;
	}

	return ldpc;
}

void ldpc_free(ldpc_coder *ldpc)
{
	if (ldpc == NULL)
		return;
	if (ldpc->buf)
		free(ldpc->buf);
	if (ldpc->mat)
		free(ldpc->mat);
	if (ldpc->id_mask)
		free(ldpc->id_mask);
	if (ldpc->bg)
		free(ldpc->bg);
	if (ldpc->tmp_buf)
		free(ldpc->tmp_buf);
	free(ldpc);
	ldpc = NULL;
}

// add some source symbols
int ldpc_encoder_add(
	ldpc_coder *ldpc,
	void *data,				// input bytes of some adding source symbols
	unsigned int off_id,	// offset to the first source symbol ID
	unsigned int num_sym)	// number of adding source symbols
{
	unsigned int num_src = ldpc->num_src;
	if (ldpc->loaded >= num_src)
		return LDPC_ADD_END;	// no need to add more.
	if ((off_id >= num_src) || (off_id + num_sym > num_src))
		return LDPC_ADD_PARA;	// given symbol ID or range is invalid.
	if (ldpc->max_bg != 0)
		return LDPC_ADD_DIF;	// decoder is different.

	int ret;
	unsigned int esi;	// Encoding Symbol ID
	unsigned int symbol_size = ldpc->sym_size;
	unsigned int align_size = ldpc->al_size;
	uint64_t *id_mask = ldpc->id_mask;
	unsigned char *data_p = data;
	unsigned char *buf = ldpc->buf;
	unsigned char *buf_p = buf + (size_t)align_size * off_id;

	ret = LDPC_ADD_OK;
	for (esi = off_id; esi < off_id + num_sym; esi++){	// source symbol ID
		if (bitmask_check(id_mask, esi) != 0){
			ret = LDPC_ADD_IGN;	// This source symbol was added already.

		} else {
			// copy source symbol to aligned buffer
			memcpy(buf_p, data_p, symbol_size);
			bitmask_set(id_mask, esi);
			ldpc->loaded += 1;	// count added symbols
		}

		// goto next source symbol
		buf_p += align_size;
		data_p += symbol_size;
	}

	// When all source symbols were added, it generates all repair symbols at once.
	if (ldpc->loaded >= num_src){
		unsigned int *entry = ldpc->mat;
		unsigned int entry_max = ldpc->num_mat;
		unsigned int row_start, row_degree;
		unsigned int row_id, column_id, node_id;
#ifdef TRIANGLE_SCHEME	// LDPC-Triangle
		unsigned int j, l;
		unsigned int pmms_state = ldpc->seed;	// seed of PRNG
#endif

		// matrix was sorted by rows after creation
		row_start = 0;
		while (row_start < entry_max){
			// XOR source symbols
			for (row_degree = 0; row_start + row_degree < entry_max; row_degree++){
				column_id = entry[0];
				row_id = entry[1];
				if (row_degree == 0){
					node_id = row_id;
					buf_p = buf + (size_t)align_size * (num_src + row_id);
					memcpy(buf_p, buf + (size_t)align_size * column_id, align_size);
				} else if (row_id != node_id){
					break;
				} else {
					align_xor(buf_p, buf + (size_t)align_size * column_id, align_size);
				}
				entry += 2;
			}
			//printf("row %u, start = %u, degree = %u\n", node_id, row_start, row_degree);

			// XOR previous repair symbol
			if (node_id > 0){

#ifdef TRIANGLE_SCHEME	// LDPC-Triangle has additional 1s in right side.
				// fill the triangle
				j = node_id - 1;
				for (l = 0; l < j; l++){	// limit the number of "1s" added
					j = pmms_rand(&pmms_state, j);
					align_xor(buf_p, buf + (size_t)align_size * (num_src + j), align_size);
				}
#endif

				align_xor(buf_p, buf_p - align_size, align_size);	// staircase
			}

			row_start += row_degree;
		}
		free_matrix(ldpc);	// release matrix
	}

	return ret;
}

// create some encoding symbols at once
// When off_id < number of source symbols, source symbol is restored.
// When off_id >= number of source symbols, repair symbol is created.
int ldpc_encoder_create(
	ldpc_coder *ldpc,
	void *data,				// output bytes of some created symbols
	unsigned int off_id,	// offset to the first encoding symbol ID
	unsigned int num_sym)	// number of creating symbols
{
	unsigned int num_src = ldpc->num_src;
	if (ldpc->loaded < num_src)
		return LDPC_ENC_LACK;	// need to add source symbols at first
	if (off_id + num_sym > num_src + ldpc->num_rep)
		return LDPC_ADD_PARA;	// given symbol ID or range is invalid.
	if (ldpc->max_bg != 0)
		return LDPC_ADD_DIF;	// decoder is different.

	unsigned int esi;	// Encoding Symbol ID
	unsigned int symbol_size = ldpc->sym_size;
	unsigned int align_size = ldpc->al_size;
	unsigned char *buf_p = ldpc->buf + (size_t)align_size * off_id;
	unsigned char *data_p = data;

	for (esi = off_id; esi < off_id + num_sym; esi++){
		// copy encoding symbol from aligned buffer
		memcpy(data_p, buf_p, symbol_size);

		buf_p += align_size;
		data_p += symbol_size;	// goto next symbol
	}

	return 0;
}


ldpc_coder *ldpc_decoder_new(
	unsigned int num_source,	// number of source symbols
	unsigned int num_repair,	// number of repair symbols
	unsigned int symbol_size)	// size of each source symbol in bytes
{
	// Becasue row weight >= 2, source symbols >= 3.
	if ((num_source < 3) || (symbol_size < 1))
		return NULL;

	ldpc_coder *ldpc = calloc(1, sizeof(ldpc_coder));
	if (ldpc == NULL)
		return NULL;

	// Common Elements
	ldpc->num_src = num_source;
	ldpc->num_rep = num_repair;
	ldpc->sym_size = symbol_size;
	ldpc->al_size = (symbol_size + SYMBOL_ALIGN - 1) & ~(SYMBOL_ALIGN - 1);

	// allocate buffer for working space
	if (allocate_buffer(ldpc) != 0){
		ldpc_free(ldpc);
		return NULL;
	}
	// In parity check matrix, sum of all symbols on each row are zero.
	ldpc->tmp_buf = calloc(num_repair, ldpc->al_size);
	if (ldpc->tmp_buf == NULL){
		ldpc_free(ldpc);
		return NULL;
	}

	// construct matrix
	if (create_matrix(ldpc) != 0){
		ldpc_free(ldpc);
		return NULL;
	}

	// setup list of encoding symbols
	ldpc->id_mask = calloc(bitmask_len(num_source + num_repair), sizeof(uint64_t));
	if (ldpc->id_mask == NULL){
		ldpc_free(ldpc);
		return NULL;
	}

	// create bipartite graph from parity check matrix
	if (create_bipartite(ldpc) != 0){
		ldpc_free(ldpc);
		return NULL;
	}

	return ldpc;
}

// Return the number of possible extra symbols to recover lost symbols.
// The number of source symbols and additional symbols will be required.
// The numbers of created repair symbols and lost symbols won't affect.
unsigned int ldpc_decoder_overhead(ldpc_coder *ldpc)
{
	// It may require 3% overhead at least 3.
	unsigned int num_overhead = (ldpc->num_src + 31) / 32;
	if (num_overhead < 3)
		num_overhead = 3;

	return num_overhead;
}

int ldpc_decoder_add(
	ldpc_coder *ldpc,
	void *data,				// input bytes of an adding symbol
	unsigned int esi)		// Encoding Symbol ID
{
	unsigned int num_src = ldpc->num_src;
	unsigned int num_rep = ldpc->num_rep;
	if (ldpc->recover >= num_src)
		return LDPC_ADD_END;	// no need to add more.
	if (esi >= num_src + num_rep)
		return LDPC_ADD_PARA;	// given symbol ID is invalid.
	if (ldpc->max_bg == 0)
		return LDPC_ADD_DIF;	// encoder is different.

	unsigned int align_size = ldpc->al_size;
	uint64_t *id_mask = ldpc->id_mask;

	if (bitmask_check(id_mask, esi) != 0)
		return LDPC_ADD_IGN;	// This encoding symbol was added already.
	bitmask_set(id_mask, esi);
	ldpc->loaded += 1;	// count added symbols

	// copy to aligned buffer
	memcpy(ldpc->buf + (size_t)align_size * esi, data, ldpc->sym_size);
	if (esi < num_src){
		ldpc->recover += 1;	// count restored source symbols
		//printf("recover %u/%u\n", ldpc->recover, num_src);
		if (ldpc->recover >= num_src){
			// Release work space, as it won't add symbols anymore.
			free_bipartite(ldpc);
			free(ldpc->id_mask);
			ldpc->id_mask = NULL;
			return LDPC_ADD_END;
		}
	}

	return LDPC_ADD_OK;
}

// Try to solve by Iterative Decoding
int ldpc_decoder_solve_iterative(ldpc_coder *ldpc)
{
	unsigned int num_src = ldpc->num_src;
	if (ldpc->recover >= num_src)
		return LDPC_ENC_OK;		// solved already
	if (ldpc->loaded < num_src)
		return LDPC_ENC_LACK;	// need more symbols
	if (ldpc->max_bg == 0)
		return LDPC_ADD_DIF;	// encoder is different.

	unsigned int prev = ldpc->recover;
	do {	// Iterative Decoding with Peeling Algorithm
		if (solve_bg_peel(ldpc) == 0)
			break;
	} while (ldpc->recover < num_src);
	printf("Peeling Decoder recovered %u after %u input, total %u/%u.\n", ldpc->recover - prev, ldpc->loaded, ldpc->recover, num_src);

	if (ldpc->recover < num_src)
		return LDPC_ENC_LACK;	// need to add more symbols

	// Release work space, as it won't add symbols anymore.
	free_bipartite(ldpc);
	free(ldpc->id_mask);
	ldpc->id_mask = NULL;

	return LDPC_ENC_OK;
}

// Try to solve by Hybrid Decoding
int ldpc_decoder_solve_hybrid(
	ldpc_coder *ldpc,
	unsigned int threshold)		// If lost symbols are less than threshold, try Gaussian Elimination.
{
	unsigned int num_src = ldpc->num_src;
	if (ldpc->recover >= num_src)
		return LDPC_ENC_OK;		// solved already
	if (ldpc->loaded < num_src)
		return LDPC_ENC_LACK;	// need more symbols
	if (ldpc->max_bg == 0)
		return LDPC_ADD_DIF;	// encoder is different.

	unsigned int prev = ldpc->recover;
	do {	// Iterative Decoding with Peeling Algorithm at first
		if (solve_bg_peel(ldpc) == 0)
			break;
	} while (ldpc->recover < num_src);
	printf("Peeling Decoder recovered %u after %u input, total %u/%u.\n", ldpc->recover - prev, ldpc->loaded, ldpc->recover, num_src);

	if ((ldpc->recover < num_src) && (num_src <= ldpc->recover + threshold)){
		// Gaussian Elimination once at last.
		prev = ldpc->recover;
		if (solve_bg_ge(ldpc) != 0)
			return LDPC_ENC_ERR;
		printf("Gaussian Elimination recovered %d after %u input, total %u/%u.\n", ldpc->recover - prev, ldpc->loaded, ldpc->recover, num_src);
	}

	if (ldpc->recover < num_src)
		return LDPC_ENC_LACK;	// need to add more symbols

	// Release work space, as it won't add symbols anymore.
	free_bipartite(ldpc);
	free(ldpc->id_mask);
	ldpc->id_mask = NULL;

	return LDPC_ENC_OK;
}

// recover some source symbols at once
int ldpc_decoder_recover(
	ldpc_coder *ldpc,
	void *data,				// output bytes of some recovered symbols
	unsigned int off_id,	// offset to the first source symbol ID
	unsigned int num_sym)	// number of recovering symbols
{
	unsigned int num_src = ldpc->num_src;
	if (ldpc->loaded < num_src)
		return LDPC_ENC_LACK;	// need to add source symbols at first
	if ((off_id >= num_src) || (off_id + num_sym > num_src))
		return LDPC_ADD_PARA;	// given symbols ID or range is invalid.
	if (ldpc->max_bg == 0)
		return LDPC_ADD_DIF;	// encoder is different.

	unsigned int esi;	// Encoding Symbol ID
	unsigned int symbol_size = ldpc->sym_size;
	unsigned int align_size = ldpc->al_size;
	unsigned char *data_p = data;
	unsigned char *buf_p = ldpc->buf + (size_t)align_size * off_id;

	// just copy recovered symbols
	for (esi = off_id; esi < off_id + num_sym; esi++){
		//printf("restore source symbol ID = %u\n", esi);
		memcpy(data_p, buf_p, symbol_size);

		// goto next symbol
		buf_p += align_size;
		data_p += symbol_size;
	}

	return LDPC_ENC_OK;
}

