/*
MIT License
Copyright (c) 2026 Yutaka Sawada
*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "staircase.h"
#include "matrix.h"
#include "prng.h"


/*
each entry has 2 4-byte integers.
Put column_id at first, then row_id next.
As 8-byte integer, row_id is upper 32-bit, column_id is lower 32-bit.
This is good to sort by rows.
*/
static void matrix_insert_entry(ldpc_coder *ldpc, unsigned int row_id, unsigned int column_id)
{
	unsigned int *entry = ldpc->mat;
	unsigned int pos = ldpc->num_mat;

	if (pos >= ldpc->max_mat){
		printf("Error: too many entories in matrix\n");
		return;
	}

	entry += pos * 2;
	entry[0] = column_id;
	entry[1] = row_id;

	ldpc->num_mat += 1;
}

static int matrix_has_entry(ldpc_coder *ldpc, unsigned int row_id, unsigned int column_id)
{
	unsigned int *entry = ldpc->mat;
	unsigned int max = ldpc->num_mat;
	unsigned int i;

	for (i = 0; i < max; i++){
		if (entry[0] == column_id){
			if (entry[1] == row_id){
				return 1;
			}
		}
		entry += 2;	// goto next entry
	}

	return 0;
}

static unsigned int degree_of_row(ldpc_coder *ldpc, unsigned int row_id)
{
	unsigned int *entry = ldpc->mat;
	unsigned int max = ldpc->num_mat;
	unsigned int i, degree;

	degree = 0;
	for (i = 0; i < max; i++){
		if (entry[1] == row_id){
			degree++;
		}
		entry += 2;	// goto next entry
	}

	return degree;
}

static int list_has_entry(unsigned int *list, unsigned int len, unsigned int id)
{
	for (unsigned int i = 0; i < len; i++){
		if (list[i] == id)
			return 1;
	}
	return 0;
}

// Returns 0xffffffff = degree 0, 0xfffffffe = degree 2 or more, position of degree i
static unsigned int state_of_row(ldpc_coder *ldpc, unsigned int row_id)
{
	unsigned int *entry = ldpc->mat;
	unsigned int max = ldpc->num_mat;
	unsigned int i, degree, ret;

	ret = 0xffffffff;
	degree = 0;
	for (i = 0; i < max; i++){
		if (entry[1] == row_id){
			degree++;
			if (degree == 1){
				ret = entry[0];
			} else if (degree == 2){
				return 0xfffffffe;
			}
		}
		entry += 2;	// goto next entry
	}

	return ret;
}


// compare function for C runtime library
static int compare_uint64(const void *elem1, const void *elem2)
{
	uint64_t val1 = *((uint64_t *)elem1);
	uint64_t val2 = *((uint64_t *)elem2);

	if (val1 < val2)
		return -1;
	if (val1 > val2)
		return 1;
	return 0;
}

static void sort_matrix_row(ldpc_coder *ldpc)
{
	// sort entries by row index
	qsort(ldpc->mat, ldpc->num_mat, sizeof(uint64_t), compare_uint64);
}

// show entries in every rows
void print_matrix_row(ldpc_coder *ldpc)
{
	unsigned int *entry = ldpc->mat;
	unsigned int max = ldpc->num_mat;
	unsigned int i;
	unsigned int row_id, column_id, old_id;

	if (max == 0)
		return;

	printf(" %u entries / max %u\n", max, ldpc->max_mat);
	old_id = 0xffffffff;
	for (i = 0; i < max; i++){
		column_id = entry[0];
		row_id = entry[1];
		if (row_id != old_id){
			if (i == 0){
				printf("row %u {%u", row_id, column_id);
			} else {
				printf("}\nrow %u {%u", row_id, column_id);
			}
			old_id = row_id;
		} else {
			printf(", %u", column_id);
		}

		entry += 2;	// goto next entry
	}
	printf("}\n");
}

// swap upper and lower 32-bit
static int compare_uint64r(const void *elem1, const void *elem2)
{
	uint64_t val1 = *((uint64_t *)elem1);
	uint64_t val2 = *((uint64_t *)elem2);

	val1 = (val1 << 32) | (val1 >> 32);
	val2 = (val2 << 32) | (val2 >> 32);

	if (val1 < val2)
		return -1;
	if (val1 > val2)
		return 1;
	return 0;
}

static void sort_matrix_column(ldpc_coder *ldpc)
{
	// sort entries by row index
	qsort(ldpc->mat, ldpc->num_mat, sizeof(uint64_t), compare_uint64r);
}

// show entries in every columns
void print_matrix_column(ldpc_coder *ldpc)
{
	unsigned int *entry = ldpc->mat;
	unsigned int max = ldpc->num_mat;
	unsigned int i;
	unsigned int row_id, column_id, old_id;

	if (max == 0)
		return;

	printf(" %u entries / max %u\n", max, ldpc->max_mat);
	old_id = 0xffffffff;
	for (i = 0; i < max; i++){
		column_id = entry[0];
		row_id = entry[1];
		if (column_id != old_id){
			if (i == 0){
				printf("column %u {%u", column_id, row_id);
			} else {
				printf("}\ncolumn %u {%u", column_id, row_id);
			}
			old_id = column_id;
		} else {
			printf(", %u", row_id);
		}

		entry += 2;	// goto next entry
	}
	printf("}\n");
}

void print_matrix(ldpc_coder *ldpc)
{
	unsigned int *entry = ldpc->mat;
	unsigned int max = ldpc->num_mat;
	unsigned int i;
	unsigned int row_id, column_id;

	if (max == 0)
		return;

	printf(" %u entries / max %u\n", max, ldpc->max_mat);
	for (i = 0; i < max; i++){
		column_id = entry[0];
		row_id = entry[1];
		printf("{%u, %u} ", row_id, column_id);
		if (i % 10 == 9)
			printf("\n");

		entry += 2;	// goto next entry
	}
	if (i % 10 != 0)
		printf("\n");
}

// Left side matrix only
// returns 0 = success, others = fail
int create_matrix(ldpc_coder *ldpc)
{
	unsigned int k = ldpc->num_src;	// number of source symbols (column)
	unsigned int r = ldpc->num_rep;	// number of repair symbols (row)
	unsigned int N1 = COLUMN_WEIGHT;
	unsigned int i;		// row index or temporary variable
	unsigned int j;		// column index
	unsigned int h;		// temporary variable
	unsigned int t;		// left limit within the list of possible choices u[]
	unsigned int *u;	// table used to have a homogeneous 1 distrib.

	if (r == 1){
		N1 = 1;	// One repair symbol is XOR of all source symbols.
	} else if (N1 >= r){
		N1 = r - 1;	// If density is full, reduce weight by one.
	}
	unsigned int total_weight = N1 * k;
	unsigned int put_list[COLUMN_WEIGHT];
	unsigned int pmms_state = PRNG_SEED;	// seed of PRNG

	u = malloc(total_weight * sizeof(unsigned int));
	if (u == NULL)
		return 1;

	// Initialize a list of all possible choices in order to guarantee a homogeneous "1" distribution
	for (h = 0; h < total_weight; h++){
		u[h] = h % r;
	}

	// Initialize the matrix with N1 "1s" per column, homogeneously
	t = 0;
	for (j = 0; j < k; j++) {	// for each source symbol column
		for (h = 0; h < N1; h++) {	// add N1 "1s"
			// check that valid available choices remain
			for (i = t; i < total_weight && list_has_entry(put_list, h, u[i]); i++);
			if (i < total_weight) {
				// choose one index within the list of possible choices
				do {
					i = t + pmms_rand(&pmms_state, total_weight - t);
				} while (list_has_entry(put_list, h, u[i]));
				matrix_insert_entry(ldpc, u[i], j);
				put_list[h] = u[i];

				// replace with u[t] which has never been chosen
				u[i] = u[t];
				t++;
			} else {
				// no choice left, choose one randomly
				do {
					i = pmms_rand(&pmms_state, r);
				} while (list_has_entry(put_list, h, i));
				matrix_insert_entry(ldpc, i, j);
				put_list[h] = i;
				//printf("no choice left, j = %u, h = %u, i = %u\n", j, h, i);
			}
		}
	}
	free(u);

	// Add extra bits to avoid rows with less than two "1s".
	// This is needed when the code rate is smaller than 2/(2+N1)
	if (total_weight < r * 3){	// A row wight may be less than 3.
		for (i = 0; i < r; i++) {	// for each row
			h = state_of_row(ldpc, i);
			if (h == 0xffffffff) {
				j = pmms_rand(&pmms_state, k);
				matrix_insert_entry(ldpc, i, j);
				h = j;
			}
			if (h != 0xfffffffe) {
				do {
					j = pmms_rand(&pmms_state, k);
				} while (j == h);
				matrix_insert_entry(ldpc, i, j);
			}
		}
	}
#ifdef TRIANGLE_SCHEME
	ldpc->seed = pmms_state;	// store seed of PRNG to create right matrix of LDPC-Triangle
#endif

	//printf("1s in left matrix = %u, ", ldpc->num_mat);
	//printf("average degree = %g\n", (double)ldpc->num_mat / (double)r);

	//print_matrix(ldpc);

	sort_matrix_row(ldpc);	// sort matrix by rows at first
	//print_matrix_row(ldpc);

	//sort_matrix_column(ldpc);
	//print_matrix_column(ldpc);

	return 0;
}

void free_matrix(ldpc_coder *ldpc)
{
	if (ldpc->mat){
		free(ldpc->mat);
		ldpc->mat = NULL;
	}
	ldpc->num_mat = 0;
	ldpc->max_mat = 0;
}

// returns 0 = success, others = fail
int allocate_buffer(ldpc_coder *ldpc)
{
	unsigned int num_src = ldpc->num_src;
	unsigned int num_rep = ldpc->num_rep;

	// allocate aligned memory
	size_t alloc_size = (size_t)ldpc->al_size * ((size_t)num_src + (size_t)num_rep);
	//printf("buffer size = %zd\n", alloc_size);
	ldpc->buf = malloc(alloc_size);
	if (ldpc->buf == NULL)
		return 1;

	// number of entries depends on weight
	if (num_src * COLUMN_WEIGHT < num_rep * 3){
		// When there is a row of weight 2, it may need more for "no choice" case.
		alloc_size = num_rep * 3;
	} else {
		alloc_size = num_src * COLUMN_WEIGHT;
	}
	ldpc->max_mat = (unsigned int)alloc_size;
	alloc_size *= sizeof(unsigned int) * 2;	// each entry has 2 integers (row index, column index)
	//printf("matrix size = %zd\n", alloc_size);
	ldpc->mat = malloc(alloc_size);
	if (ldpc->mat == NULL)
		return 2;
	ldpc->num_mat = 0;

	return 0;
}

