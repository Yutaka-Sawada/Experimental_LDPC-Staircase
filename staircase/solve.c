/*
MIT License
Copyright (c) 2026 Yutaka Sawada
*/
#include <stdio.h>
#include <stdlib.h>

#include "staircase.h"
#include "bipart.h"
#include "bitmask.h"
#include "solve.h"
#include "util.h"


/*
Peeling Algorithm
Try to recover input symbols by searching nodes with single edge

It returns zero, when it finished recovery.
Or else, it returns non-zero for partial recovery.
*/
unsigned int solve_bg_peel(ldpc_coder *ldpc)
{
	unsigned int i, id;
	unsigned int node_start, node_id, node_len;
	unsigned int edge_count, lost_id;
	unsigned int slide_size;
	unsigned int num_src = ldpc->num_src;
	uint64_t *id_mask = ldpc->id_mask;
	unsigned int list_len = ldpc->num_bg;
	unsigned int *node_list = ldpc->bg;
	unsigned int align_size = ldpc->al_size;
	unsigned char *buf = ldpc->buf;
	unsigned char *tmp_buf = ldpc->tmp_buf;
	//print_bg(ldpc);

	// test every encoding symbols
	slide_size = 0;
	node_start = 0;
	while (node_start < list_len){
		node_id = node_list[node_start];
		node_len = node_list[node_start + 1];

		// test neighbor symbol
		edge_count = 0;
		for (i = 0; i < node_len; i++){
			id = node_list[node_start + 2 + i];
			if (bitmask_check(id_mask, id) == 0){	// The neighbor symbol is missing.
				edge_count++;
				lost_id = id;
			} else {
				// If the neighbor symbol exists, XOR symbol and remove the edge.
				align_xor(tmp_buf + (size_t)align_size * node_id, buf + (size_t)align_size * id, align_size);
				//printf("XOR neighbor symbol %u -> stored symbol %u\n", id, node_id);
				node_list[node_start + 2 + i] = ERASE_ID;	// eraser mark
			}
		}
		//printf("node = %u, start = %u, edge = %u / %u\n", node_id, node_start, edge_count, node_len);

		// If there is only one edge, the data is recovered symbol.
		if (edge_count == 1){
			// copy node's sum to original symbol position
			memcpy(buf + (size_t)align_size * lost_id, tmp_buf + (size_t)align_size * node_id, align_size);
			//printf("symbol %u is recovered from node sum %u\n", lost_id, node_id);
			bitmask_set(id_mask, lost_id);
			if (lost_id < num_src){
				ldpc->recover += 1;	// count restored source symbols
				if (ldpc->recover >= num_src)
					return 0;	// No need to recover repair symbols, when all source symbols were recovered.
			}
			edge_count = 0;
		}

		if (edge_count == 0){	// remove this node from bipartite graph
			slide_size += 2 + node_len;
		} else {
			if (slide_size > 0)	// slide this node to front
				node_list[node_start - slide_size] = node_id;
			if ((slide_size > 0) || (edge_count < node_len)){
				node_list[node_start - slide_size + 1] = edge_count;
				id = node_start - slide_size + 2;
				for (i = 0; i < node_len; i++){
					lost_id = node_list[node_start + 2 + i];
					if (lost_id != ERASE_ID){
						node_list[id] = lost_id;
						id++;
					}
				}
			}
			if (edge_count < node_len)
				slide_size += node_len - edge_count;
		}
		node_start += 2 + node_len;	// goto next node
	}

	if (slide_size > 0){
		//printf("slide_size = %u\n", slide_size);
		ldpc->num_bg = list_len - slide_size;
		//print_bg(ldpc);
	}
	return slide_size;
}

/*
Try to solve by Gaussian Elimination
Gaussian Elimination is very slow. O(n power 3)
Before call this, solve_bg_peel() must return 0 to ensure that left nodes don't exist.

It returns zero for successful recovery.
Or else, it returns non-zero for error.
*/
unsigned int solve_bg_ge(ldpc_coder *ldpc)
{
	unsigned int i, id;
	unsigned int num_col, num_row, num_total, num_lost;
	unsigned int node_start, node_id, node_len;
	unsigned int num_src = ldpc->num_src;
	uint64_t *id_mask = ldpc->id_mask;
	unsigned int list_len = ldpc->num_bg;
	unsigned int *node_list = ldpc->bg;
	unsigned int *map_col, *map_rev;

	// It checked "ldpc->loaded >= num_src" before calling this.
	num_total = num_src + ldpc->num_rep;
	i = bitmask_pop(id_mask, bitmask_len(num_total));	// total number of available symbols
	num_lost = num_total - i;	// number of lost symbols
	//printf("total = %u, exist = %u, lost = %u\n", num_total, i, num_lost);
	//print_bg(ldpc);

	// map of symbol ID to column ID of matrix
	map_col = calloc(num_total, sizeof(unsigned int));
	if (map_col == NULL)
		return 1;

	// count number of nodes and their edges
	num_row = 0;
	node_start = 0;
	while (node_start < list_len){
		node_id = node_list[node_start];
		node_len = node_list[node_start + 1];

		// test edges
		for (i = 0; i < node_len; i++){
			id = node_list[node_start + 2 + i];
			map_col[id] += 1;
		}
		num_row++;

		node_start += 2 + node_len;	// goto next node
	}
	if (num_lost > num_row){	// missing symbols are more than available nodes
		free(map_col);
		return 0;	// exit without modifying bipartite graph
	}
/*
	printf("count %u symbol IDs in %u rows -> %u symbols lost\n", num_total, num_row, num_lost);
	for (i = 0; i < num_total; i++)
		printf(" %u", map_col[i]);
	printf("\n");
*/

	// reverse map of column ID of matrix to symbol ID
	map_rev = malloc(sizeof(unsigned int) * num_lost);
	if (map_rev == NULL){
		free(map_col);
		return 2;
	}

	// count number of columns
	num_col = 0;
	for (i = 0; i < num_total; i++){
		id = map_col[i];
		if (id == 0){
			//map_col[i] = 0xffffffff;	// mark of ignored symbol for debug print
			continue;
		}
		// missing symbol
		map_col[i] = num_col;
		map_rev[num_col] = i;
		num_col++;
	}
	//printf("num_row = %u, num_col = %u\n", num_row, num_col);
	if (num_col < num_lost){	// available symbols are too few
		free(map_col);
		free(map_rev);
		return 0;	// exit without modifying bipartite graph
	}

/*
	// mapping lost symbol ID to column ID of matrix
	printf("map %u symbol IDs -> %u column IDs\n", num_total, num_col);
	for (i = 0; i < num_total; i++)
		printf(" %d", map_col[i]);
	printf("\n");
	printf("reverse map %u column IDs -> %u symbol IDs\n", num_col, num_total);
	for (i = 0; i < num_col; i++)
		printf(" %u", map_rev[i]);
	printf("\n");
*/

	// allocate three lists at once
	unsigned int *store_list, *degree_list, *order_list;
	store_list = malloc(sizeof(unsigned int) * num_row * 3);
	if (store_list == NULL){
		free(map_col);
		free(map_rev);
		return 3;
	}
	order_list = store_list + num_row;
	degree_list = order_list + num_row;

	// allocate memory for matrix
	unsigned int col_id, row_id;
	unsigned int int_col = (num_col + IDXBITS - 1) / IDXBITS;	// number of integers for columns
	uint64_t *matrix, *row_p;
	matrix = calloc(int_col * num_row, sizeof(uint64_t));
	if (matrix == NULL){
		free(map_col);
		free(map_rev);
		free(store_list);
		return 4;
	}
	//printf("bit matrix's %u columns -> %u integers, %u rows, %g KB\n", num_col, int_col, num_row, (double)(int_col * num_row) / 125.0);

	// construct matrix from bipartite graph
	row_id = 0;
	row_p = matrix;
	node_start = 0;
	while (node_start < list_len){
		node_id = node_list[node_start];
		node_len = node_list[node_start + 1];

		// test node
		degree_list[row_id] = node_len;
		store_list[row_id] = node_id;	// store position in tmp_buf
		row_id++;

		// test edges
		for (i = 0; i < node_len; i++){
			id = node_list[node_start + 2 + i];
			bitmask_set(row_p, map_col[id]);
		}
		row_p += int_col;

		node_start += 2 + node_len;	// goto next node
	}
	free(map_col);	// No need this list anymore
	//print_bit_matrix(matrix, num_col, num_row);

	// solve equation
	unsigned int pivot_id, next_id;
	unsigned int row_degree, pivot_degree;
	unsigned int align_size = ldpc->al_size;
	unsigned char *buf = ldpc->buf;
	unsigned char *tmp_buf = ldpc->tmp_buf;
	unsigned char *src_p;
	uint64_t *pivot_p;

	// find a row with the smallest degree
	pivot_degree = 0xffffffff;
	for (i = 0; i < num_row; i++){
		row_degree = degree_list[i];
		//printf("degree_list[%u] = %u\n", i, row_degree);
		if (pivot_degree > row_degree){
			pivot_degree = row_degree;
			pivot_id = i;
		}
	}

	num_lost = 0;	// count number of resolved rows
	while (pivot_degree < 0xffffffff){
		pivot_p = matrix + int_col * pivot_id;
		col_id = bitmask_ntz(pivot_p, int_col);
		//printf("pivot_id = %u, pivot_degree = %u, col_id = %u\n", pivot_id, pivot_degree, col_id);
		order_list[pivot_id] = col_id;	// save order of rows
		degree_list[pivot_id] = 0xffffffff;	// no need degree of pivot row

		// XOR pivot_row to other rows
		pivot_degree = 0xffffffff;
		src_p = tmp_buf + (size_t)align_size * store_list[pivot_id];
		row_p = matrix;
		for (row_id = 0; row_id < num_row; row_id++){
			if (row_id == pivot_id){
				row_p += int_col;
				continue;
			}

			if (bitmask_check(row_p, col_id) != 0){	// XOR rows
				align_xor(tmp_buf + (size_t)align_size * store_list[row_id], src_p, align_size);
				if (degree_list[row_id] != 0xffffffff){	// re-calculate degree
//					for (i = 0; i < int_col; i++)
//						row_p[i] ^= pivot_p[i];
//					row_degree = bitmask_pop(row_p, int_col);
					row_degree = align_xor_pop(row_p, pivot_p, int_col);
					if (row_degree == 0){	// If row_degree is zero, it won't select this.
						row_degree = 0xffffffff;
						order_list[row_id] = 0xffffffff;	// ignore this row at recovery
					}
					degree_list[row_id] = row_degree;
					//printf("degree_list[%u] = %u\n", row_id, row_degree);
					if (pivot_degree > row_degree){
						pivot_degree = row_degree;
						next_id = row_id;
					}
				} else {
					for (i = 0; i < int_col; i++)
						row_p[i] ^= pivot_p[i];
				}

			} else if (degree_list[row_id] != 0xffffffff){	// find a row with the smallest degree
				row_degree = degree_list[row_id];
				if (pivot_degree > row_degree){
					pivot_degree = row_degree;
					next_id = row_id;
				}
			}
			row_p += int_col;
		}
		num_lost++;

/*
		printf("next_id = %u, pivot_degree = %u\n", next_id, pivot_degree);
		if (num_lost >= 11)
			print_bit_matrix(matrix, num_col, num_row);
		if (num_lost >= 12)
			break;
*/

		pivot_id = next_id;	// set next pivot
	}
	//print_bit_matrix(matrix, num_col, num_row);

	//printf("number of resolved rows = %u\n", num_lost);
	if (num_lost < num_col){
		//printf("Cannot solve equation, un-resolved = %u\n", num_col - num_lost);
		// recover missing source symbols if possible
		num_total = 0;	// count number of edges
		num_lost = 0;	// count number of nodes
		row_p = matrix;
		for (row_id = 0; row_id < num_row; row_id++){
			row_degree = bitmask_pop(row_p, int_col);
			//printf("degree_list[%u] = %u\n", row_id, row_degree);
			if (row_degree == 1){
				col_id = order_list[row_id];	// load order of rows
				id = map_rev[col_id];
				memcpy(buf + (size_t)align_size * id, tmp_buf + (size_t)align_size * store_list[row_id], align_size);
				//printf("symbol %u (column %u) is recovered from row %u\n", id, col_id, row_id);
				bitmask_set(id_mask, id);
				if (id < num_src){
					ldpc->recover += 1;	// count restored source symbols
					if (ldpc->recover >= num_src){
						free(map_rev);
						free(store_list);
						free(matrix);
						return 0;	// No need to recover repair symbols, when all source symbols were recovered.
					}
				}
				row_degree = 0;
			}
			degree_list[row_id] = row_degree;
			num_total += row_degree;
			if (row_degree > 0)
				num_lost++;
			row_p += int_col;
		}
		//printf("edges = %u, nodes = %u\n", num_total, num_lost);
		// re-allocate memory
		free(ldpc->bg);
		list_len = num_total + num_lost * 2;
		ldpc->bg = malloc(list_len * sizeof(unsigned int));
		if (ldpc->bg == NULL){
			free(map_rev);
			free(store_list);
			free(matrix);
			return 5;
		}
		node_list = ldpc->bg;
		ldpc->max_bg = list_len;
		ldpc->num_bg = 0;
		// re-construct bipartite graph from matrix
		uint64_t tmp;
		node_start = 0;
		row_p = matrix;
		for (row_id = 0; row_id < num_row; row_id++){
			node_len = degree_list[row_id];
			if (node_len == 0){
				row_p += int_col;
				continue;
			}
			node_id = store_list[row_id];
			//printf("row_id = %u, node_id = %u, node_len = %u\n", row_id, node_id, node_len);
			node_list[node_start] = node_id;
			node_list[node_start + 1] = node_len;
			node_start += 2;
			for (id = 0; id < int_col; id++){
				tmp = row_p[id];
				if (tmp == 0)
					continue;
				for (i = 0; i < IDXBITS; i++){
					if (tmp & 1){
						col_id = id * IDXBITS + i;
						pivot_id = map_rev[col_id];
						//printf("col_id = %u, sumbol id = %u\n", col_id, pivot_id);
						node_list[node_start] = pivot_id;
						node_start++;
					}
					tmp = tmp >> 1;
				}
			}
			row_p += int_col;
		}
		ldpc->num_bg = node_start;
		//print_bg(ldpc);
		// release memory
		free(map_rev);
		free(store_list);
		free(matrix);
		return 0;
	}

	// If it solved succesfully, no need matrix anymore.
	free(matrix);

	// recover missing source symbols
	for (row_id = 0; row_id < num_row; row_id++){
		col_id = order_list[row_id];	// load order of rows
		if (col_id != 0xffffffff){
			id = map_rev[col_id];
			memcpy(buf + (size_t)align_size * id, tmp_buf + (size_t)align_size * store_list[row_id], align_size);
			//printf("symbol %u (column %u) is recovered from row %u\n", id, col_id, row_id);
			bitmask_set(id_mask, id);
			if (id < num_src){
				ldpc->recover += 1;	// count restored source symbols
				if (ldpc->recover >= num_src)
					break;	// No need to recover repair symbols, when all source symbols were recovered.
			}
		}
	}

	free(map_rev);
	free(store_list);
	return 0;
}

