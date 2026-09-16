#ifndef LDPC_MATRIX_H
#define LDPC_MATRIX_H

void print_matrix_row(ldpc_coder *ldpc);

int create_matrix(ldpc_coder *ldpc);

void free_matrix(ldpc_coder *ldpc);

int allocate_buffer(ldpc_coder *ldpc);

#endif
