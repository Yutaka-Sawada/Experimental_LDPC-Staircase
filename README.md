# Experimental_LDPC-Staircase

This is an experimental implementation of LDPC Staircase and Triangle. 
It only supports core feature (encoder and decoder) of RFC 5170.

The implementation consists of files in `staircase` directory. 
It's possible to setup encoder/decoder, some symbols, and test recovery. 
For the usage, refer `staircase.h`. 
I put a sample `main.c` to use the Recovery Codes.

## LDPC-Triangle scheme

Technically, LDPC-Staircase and LDPC-Triangle are similar. 
Only the construction of generator matrix is different. 
While LDPC-Triangle is a little slower, 
it may keep erasure correction capability at many loss of repair symbols. 
Though I wrote code for LDPC-Triangle, it's disabled by default. 
If you want to test LDPC-Triangle, you need to edit source code.

## Multiple decoding methods

For recovery, I implemented Peeling Algorithm and Gaussian Elimination.
- Peeling Algorithm (called as Peeling Decoder or Iterative Decoding) is fast, but requires many overheads.
- Gaussian Elimination is slow, but requires less overheads.

If you want to test their speed, you need to edit source code.

## Caution

Because I didn't compare result of encoded symbols, 
there may be a compatibility issue with other implementations of LDPC-Staircase.

If you want to use this for practical usage, you need to check and test by yourself.

