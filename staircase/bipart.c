/*
MIT License
Copyright (c) 2026 Yutaka Sawada
*/
#include <stdio.h>
#include <stdlib.h>

#include "staircase.h"
#include "bipart.h"
#include "bitmask.h"
#include "matrix.h"
#include "prng.h"
#include "util.h"


/*
Make this from Parity Check Matrix's rows and columns

construction of Bipartite Graph for source symbols and repair symbols:
{
	ID of the row,
	length of following items (number of valid IDs may be fewer than this value.),
	Encoding Symbol ID of 1st column,
	Encoding Symbol ID of 2nd column,
	...
	Encoding Symbol ID of N-th column
} for each node continuously
*/

// Create bipartite graph from parity check matrix
// returns 0 = success, others = fail
int create_bipartite(ldpc_coder *ldpc)
{
	unsigned int num_src = ldpc->num_src;
	unsigned int num_rep = ldpc->num_rep;
	unsigned int entry_max = ldpc->num_mat;

	//print_matrix_row(ldpc);

	// allocate memory for bipartite graph
	size_t alloc_size = entry_max + num_rep * 2 + num_rep * 2 - 1;
#ifdef TRIANGLE_SCHEME	// LDPC-Triangle has additional 1s.
	printf("LDPC Triangle scheme may require %u more entries.\n", num_rep * (num_rep - 1) / 4);
	alloc_size += num_rep * (num_rep - 1) / 4;	// half of triangle
#endif
	ldpc->max_bg = (unsigned int)alloc_size;
	alloc_size *= sizeof(unsigned int);
	//printf("graph size = %zd\n", alloc_size);
	ldpc->bg = malloc(alloc_size);
	if (ldpc->bg == NULL)
		return 1;

	unsigned int row_start, row_degree;
	unsigned int row_id, column_id;
	unsigned int node_start, node_id, node_len;
	unsigned int *node_list = ldpc->bg;
	unsigned int *mat = ldpc->mat;
#ifdef TRIANGLE_SCHEME	// LDPC-Triangle
	unsigned int j, l;
	unsigned int pmms_state = ldpc->seed;	// seed of PRNG
#endif

	// matrix was sorted by rows after creation
	node_start = 0;
	row_start = 0;
	while (row_start < entry_max){
		// count source symbols in the row
		for (row_degree = 0; row_start + row_degree < entry_max; row_degree++){
			column_id = mat[(row_start + row_degree) * 2];
			row_id = mat[(row_start + row_degree) * 2 + 1];
			if (row_degree == 0){
				node_id = row_id;
			} else if (row_id != node_id){
				break;
			}
			// set 1s position in left matrix
			node_list[node_start + 2 + row_degree] = column_id;
		}
		//printf("row %u, start = %u, degree = %u\n", node_id, row_start, row_degree);

		node_len = row_degree;
		if (node_id > 0){	// add previous repair symbol

#ifdef TRIANGLE_SCHEME	// LDPC-Triangle has additional 1s in right side.
			// fill the triangle
			j = node_id - 1;
			for (l = 0; l < j; l++){	// limit the number of "1s" added
				if (node_start + 2 + node_len > ldpc->max_bg){
					printf("bipartite graph has more than %u items\n", ldpc->max_bg);
					return 2;
				}
				j = pmms_rand(&pmms_state, j);
				node_list[node_start + 2 + node_len] = num_src + j;
				node_len++;
			}
#endif

			node_list[node_start + 2 + node_len] = num_src + node_id - 1;	// dual diagonal
			node_len++;
		}
		// set this repair symbol
		node_list[node_start + 2 + node_len] = num_src + node_id;	// diagonal
		node_len++;
		//printf("node %u, start = %u, len = %u\n", node_id, node_start, node_len);
		node_list[node_start] = node_id;
		node_list[node_start + 1] = node_len;

		node_start += 2 + node_len;	// goto next node
		row_start += row_degree;	// goto next row
	}
	ldpc->num_bg = node_start;

	//printf("average degree = %g\n", (double)(entry_max + num_rep * 2 - 1) / num_rep);
	//print_bg(ldpc);

	free_matrix(ldpc);	// no need matrix anymore

	return 0;
}

void free_bipartite(ldpc_coder *ldpc)
{
	if (ldpc->bg){
		free(ldpc->bg);
		ldpc->bg = NULL;
	}
	ldpc->num_bg = 0;
	ldpc->max_bg = 1;	// mark of decoder

	if (ldpc->tmp_buf){	// release temporary buffer, too.
		free(ldpc->tmp_buf);
		ldpc->tmp_buf = NULL;
	}
}

void print_bg(ldpc_coder *ldpc)
{
	unsigned int i, j, id;
	unsigned int node_id, node_len, edge_count;
	unsigned int max = ldpc->num_bg;
	unsigned int *list = ldpc->bg;
	uint64_t *id_mask = ldpc->id_mask;

	if (max == 0)
		return;

	printf(" %u items / max %u\n", max, ldpc->max_bg);
	i = 0;
	while (i < max){
		node_id = list[i];
		node_len = list[i + 1];
//		if (node_id == ERASE_ID){
//			i += 2 + node_len;
//			continue;	// ignore erased node
//		}
		printf("{");
		edge_count = 0;
		for (j = 0; j < node_len; j++){
			if (edge_count != 0)
				printf(" ");
			id = list[i + 2 + j];
			if (bitmask_check(id_mask, id) != 0){
				printf("e%u", id);	// exist
			} else {
				printf("%u", id);	// missing
			}
			edge_count++;
		}
		printf("} = %u\n", node_id);
		i += 2 + node_len;
	}
}

