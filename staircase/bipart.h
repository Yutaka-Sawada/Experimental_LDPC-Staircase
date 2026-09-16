#ifndef LDPC_BIPARTITE_H
#define LDPC_BIPARTITE_H

// mark of erased node or edge
#define ERASE_ID 0xffffffff


int create_bipartite(ldpc_coder *ldpc);

void free_bipartite(ldpc_coder *ldpc);

void print_bg(ldpc_coder *ldpc);

#endif
