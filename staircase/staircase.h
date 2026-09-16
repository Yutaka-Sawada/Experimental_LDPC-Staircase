#ifndef LDPC_STAIRCASE_H
#define LDPC_STAIRCASE_H

#include <stdint.h>


/*
LDPC-Staircase is fast for iterative decoding.

LDPC-Triangle would be tough against loss of repair symbols.
When all repair symbols are available, there is no difference.
*/
//#define TRIANGLE_SCHEME	// Un-comment this, if you want to use LDPC-Triangle.

/*
Number of "1s" per column in the left side of the Parity Check Matrix
3 is fast for Iterative Decoding.
5 is good for Gaussian Elimination.
*/
#define COLUMN_WEIGHT	3	// The value should be in range of 3 ~ 10.

#define PRNG_SEED	1		// PRNG seed (1 ~ 0x7ffffffe)

#define SYMBOL_ALIGN	8	// This value must be multiple of 8.

#define LDPC_ADD_END   2	// It got enough symbols already. Repair is possible.
#define LDPC_ADD_IGN   1	// It ignored the symbol, as it was added already or useless.
#define LDPC_ADD_OK    0
#define LDPC_ADD_ERR  -1	// Fatal error (mostly insufficient memory)
#define LDPC_ADD_DIF  -2	// Either calling encoder or decoder is possible.
#define LDPC_ADD_PARA -3	// Given parameter is bad.

#define LDPC_ENC_LACK  3	// It needs to add more symbols.
#define LDPC_ENC_OK    0
#define LDPC_ENC_ERR  -1	// Fatal error (mostly insufficient memory)
#define LDPC_ENC_DIF  -2	// Either calling encoder or decoder is possible.
#define LDPC_ENC_PARA -3	// Given parameter is bad.


typedef struct {
	// source symbols
	unsigned int num_src;	// number of source symbols
	unsigned int num_rep;	// number of repair symbols
	unsigned int sym_size;	// size of each symbol in bytes

	// left side of parity check matrix
	unsigned int *mat;		// position (row index, column index) of each entry
	unsigned int num_mat;	// number of entries
	unsigned int max_mat;	// maximum number of entries
#ifdef TRIANGLE_SCHEME
	unsigned int seed;		// state of PRNG (1 ~ 0x7ffffffe)
#endif

	// encoding symbols
	unsigned int al_size;	// aligned symbol size (multiple of SYMBOL_ALIGN)
	unsigned char *buf;		// address of aligned buffer
	uint64_t *id_mask;		// list of symbol's state

	// work space to store symbols temporary
	unsigned char *tmp_buf;	// address of temporary buffer

	// nodes of neighbor symbols in bipartite graph
	unsigned int *bg;		// bipartite graph
	unsigned int num_bg;	// number of items
	unsigned int max_bg;	// maximum number of items

	// process counter
	unsigned int loaded;	// number of added symbols
	unsigned int recover;	// number of restored source symbols
} ldpc_coder;

/*
 It returns a new encoder configured with given parameters.
The number of source symbols must be more than 3.
The symbol size does not need to be a multiple of 8.
*/
ldpc_coder *ldpc_encoder_new(
	unsigned int num_source,	// number of source symbols
	unsigned int num_repair,	// number of repair symbols
	unsigned int symbol_size);	// size of each source symbol in bytes

/*
 It frees up any resources used by a decoder/encoder.
After all tasks were done, call this function.
*/
void ldpc_free(ldpc_coder *ldpc);

/*
 It adds some source symbols to the encoder.
 It returns 0 at success, see LDPC_ADD_* for others.
 It keeps list of added symbols.
If same source symbols are added, later one is ignored.
When it got enough symbols, it generates repair symbols automatically.
*/
int ldpc_encoder_add(
	ldpc_coder *ldpc,
	void *data,				// input bytes of some adding source symbols
	unsigned int off_id,	// offset to the first source symbol ID
	unsigned int num_sym);	// number of adding source symbols

/*
 It creates some encoding symbols at once.
When ESI < number of source symbols, source symbols are restored.
When ESI >= number of source symbols, repair symbols are created.
 It returns 0 at success, see LDPC_ENC_* for others.
*/
int ldpc_encoder_create(
	ldpc_coder *ldpc,
	void *data,				// output bytes of some created symbols
	unsigned int off_id,	// offset to the first encoding symbol ID
	unsigned int num_sym);	// number of creating symbols

/*
 It returns a new decoder initialized with given parameters.
These parameters must be same as used values in encoder.
*/
ldpc_coder *ldpc_decoder_new(
	unsigned int num_source,	// number of source symbols
	unsigned int num_repair,	// number of repair symbols
	unsigned int symbol_size);	// size of each source symbol in bytes

/*
 It returns how many extra symbols may be required to restore whole symbols.
This isn't a determined value, but is just an estimated value.
Adding more extra symbols than this value would be faster to decode.
*/
unsigned int ldpc_decoder_overhead(ldpc_coder *ldpc);

/*
 It adds an encoding symbol to the decoder.
 It returns 0 at success, see LDPC_ADD_* for others.
 It distinguishes the added encoding symbol by its Encoding Symbol ID.
When adding a source symbol, its ESI < number of source symbols.
When adding a repair symbol, its ESI >= number of source symbols.
*/
int ldpc_decoder_add(
	ldpc_coder *ldpc,
	void *data,				// input bytes of an adding symbol
	unsigned int esi);		// Encoding Symbol ID

/*
 It restores internal symbols from added symbols in decoder.
 It returns 0 at success, see LDPC_ENC_* for others.
When added symbols were not enough, try again after adding more symbols.
 This tries to solve by Peeling Algorithm.
It's very fast (linear time), but would require many overheads.
*/
int ldpc_decoder_solve_iterative(ldpc_coder *ldpc);

/*
 It restores internal symbols from added symbols in decoder.
 It returns 0 at success, see LDPC_ENC_* for others.
When added symbols were not enough, try again after adding more symbols.
 This tries to solve by Hybrid Decoding (Peeling and Gaussian Elimination).
Though it's not so fast, it would require fewer overheads.
 Because Gaussian Elimination is slow, it's possible to set threshold.
*/
int ldpc_decoder_solve_hybrid(
	ldpc_coder *ldpc,
	unsigned int threshold);	// If lost symbols are less than threshold, try Gaussian Elimination.

/*
 It restores some encoding symbols in the decoder.
When ESI < number of source symbols, source symbols are recovered.
When ESI >= number of source symbols, repair symbols are restored.
 It returns 0 at success, see LDPC_ENC_* for others.
*/
int ldpc_decoder_recover(
	ldpc_coder *ldpc,
	void *data,				// output bytes of some recovered symbols
	unsigned int off_id,	// offset to the first source symbol ID
	unsigned int num_sym);	// number of recovering symbols

#endif
