
#include <inttypes.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "staircase/staircase.h"


/*
PCG Random Number Generation
http://www.pcg-random.org

I use global RNGs only.
*/
static uint64_t pcg32_state, pcg32_inc;

uint32_t pcg32_random()
{
    uint64_t oldstate = pcg32_state;
    pcg32_state = oldstate * 6364136223846793005ULL + pcg32_inc;
    uint32_t xorshifted = (uint32_t)(((oldstate >> 18u) ^ oldstate) >> 27u);
    uint32_t rot = oldstate >> 59u;
    return (xorshifted >> rot) | (xorshifted << ((uint32_t)(-(int32_t)rot) & 31));
}

void pcg32_srandom(uint64_t seed, uint64_t seq)
{
	pcg32_state = 0U;
	pcg32_inc = (seq << 1u) | 1u;
	pcg32_random();
	pcg32_state += seed;
	pcg32_random();
}

uint32_t pcg32_boundedrand(uint32_t bound)
{
    uint32_t threshold = (uint32_t)(-(int32_t)bound) % bound;

    for (;;) {
        uint32_t r = pcg32_random();
        if (r >= threshold)
            return r % bound;
    }
}


/*
source_count = s#
parity_count = p#
lost_count = l#
parity_lost = lp#
each_size = d#
*/
int main(int argc, char* argv[]){
	char *p;
	char *source_buf, *parity_buf, *work_buf;
	int i, j, min, max, rv;
	int source_count = 1000, parity_count = 500;
	int lost_count = 0, parity_lost = 0;
	int input_count, ignore_count, id;
	int valid_count, ok_count;
	int *order_buf;
	unsigned int *int_p;
	unsigned int each_size = 64000;
	size_t source_size, parity_size;
	size_t malloc_size, input_size, output_size;
	double value_kb, value_mb, input_speed, output_speed;
	clock_t start, finish;
	double duration;
	ldpc_coder *ldpc;

/*
source_count = 20;
parity_count = 20;
lost_count = 18;
//parity_lost = 2;
each_size = 16;
*/

	// read input
	for (i = 1; i < argc; i++){
		p = argv[i];
		//printf("argv[%d] = %s\n", i, p);

		if ( (p[0] == 's') || (p[0] == 'S') ){
			source_count = atoi(p + 1);
		} else if ( (p[0] == 'p') || (p[0] == 'P') ){
			parity_count = atoi(p + 1);
		} else if ( (p[0] == 'l') || (p[0] == 'L') ){
			if ( (p[1] == 'p') || (p[1] == 'P') ){
				parity_lost = atoi(p + 2);
			} else {
				lost_count = atoi(p + 1);
			}

		} else if ( (p[0] == 'e') || (p[0] == 'E') ){
			each_size = atoi(p + 1);

		} else {	// invalid option
			printf(
"options: s#, p#, l#, lp#, e#\n"
"s# or S# = source count (3 ~ )\n"
"p# or P# = parity count\n"
"l# or L# = loss count\n"
"lp# or LP# = parity loss count\n"
"e# or E# = each data size\n");
			return 0;
		}
	}

	// check input
	if (source_count < 3)
		source_count = 3;
	if (parity_count <= 0)
		parity_count = 1;
	if (lost_count <= 0){
		if (parity_count >= source_count){	// 100% of source count
			lost_count = source_count;
		} else {	// 90% of parity count
			lost_count = parity_count - (parity_count + 9) / 10;
		}
	}
	if (lost_count > source_count)
		lost_count = source_count;
	if (lost_count > parity_count)
		lost_count = parity_count;
	if (parity_count - parity_lost < lost_count)
		parity_lost = parity_count - lost_count;
	if (parity_lost < 0){
		parity_lost = 0;
	} else if (parity_lost >= parity_count){
		parity_lost = parity_count - 1;
	}

	printf("source count = %d, parity count = %d, loss count = %d, parity loss = %d\n",
			source_count, parity_count, lost_count, parity_lost);
	if (each_size < 1)
		each_size = 1;
	value_kb = (double)each_size / 1000;
	value_mb = (double)each_size / 1000000.f;
	printf("each data size = %d bytes = %g KB = %g MB\n", each_size, value_kb, value_mb);

	start = clock();
	printf("\n allocate memory and setup data ...\n");

	// allocate buffer
	source_size = (size_t)each_size * source_count;
	source_buf = malloc(source_size);
	if (source_buf == NULL){
		printf("Failed: malloc, %zd\n", source_size);
		return 1;
	}
	value_kb = (double)source_size / 1000;
	value_mb = (double)source_size / 1000000.f;
	printf("source data size = %zd bytes = %g KB = %g MB\n", source_size, value_kb, value_mb);

	// fill source data with random
	max = each_size / 4;
	for (i = 0; i < source_count; i++){
		int_p = (int *)(source_buf + (size_t)each_size * i);
		pcg32_srandom(0, i);
		for (j = 0; j < max; j++)
			int_p[j] = pcg32_random();
		if (each_size & 3){	// each_size may not be multiple of 4.
			rv = pcg32_random();
			if (each_size % 4 == 1){
				source_buf[(size_t)each_size * i + (each_size & ~3)] = (char)rv;
			} else if (each_size % 4 == 2){
				source_buf[(size_t)each_size * i + (each_size & ~3)    ] = (char)rv;
				source_buf[(size_t)each_size * i + (each_size & ~3) + 1] = (char)(rv >> 8);
			} else if (each_size % 4 == 3){
				source_buf[(size_t)each_size * i + (each_size & ~3)    ] = (char)rv;
				source_buf[(size_t)each_size * i + (each_size & ~3) + 1] = (char)(rv >> 8);
				source_buf[(size_t)each_size * i + (each_size & ~3) + 2] = (char)(rv >> 16);
			}
		}
	}

	// fill parity data with zero
	parity_size = (size_t)each_size * parity_count;
	parity_buf = calloc(parity_size, 1);
	if (parity_buf == NULL){
		printf("Failed: malloc, %zd\n", parity_size);
		free(source_buf);
		return 1;
	}
	value_kb = (double)parity_size / 1000;
	value_mb = (double)parity_size / 1000000.f;
	printf("parity data size = %zd bytes = %g KB = %g MB\n", parity_size, value_kb, value_mb);

	finish = clock();
	duration = (double)(finish - start) / CLOCKS_PER_SEC;
	printf(" ... %.3f seconds\n", duration);

	printf("\n encode ...\n");
	start = clock();

	// initialize Low Density Parity Check (LDPC) Staircase encoder
	ldpc = ldpc_encoder_new(source_count, parity_count, each_size);
	if (ldpc == NULL){
		printf("Failed: ldpc_encoder_new\n");
		free(source_buf);
		free(parity_buf);
		return 1;
	}


	// add all source data at once
	rv = ldpc_encoder_add(ldpc, source_buf, 0, source_count);
	if (rv < 0){
		printf("Failed: ldpc_encoder_add, %d\n", rv);
		free(source_buf);
		free(parity_buf);
		ldpc_free(ldpc);
		return 1;
	}

/*
	// add all source data in 2 times
	rv = ldpc_encoder_add(ldpc, source_buf, 0, source_count / 2);
	if (rv < 0){
		printf("Failed: ldpc_encoder_add, %d\n", rv);
		free(source_buf);
		free(parity_buf);
		ldpc_free(ldpc);
		return 1;
	}
	// adding same symbol is ignored.
	rv = ldpc_encoder_add(ldpc, source_buf + (size_t)each_size * (source_count / 3),
			 source_count / 3, source_count - source_count / 3);
	if (rv < 0){
		printf("Failed: ldpc_encoder_add, %d\n", rv);
		free(source_buf);
		free(parity_buf);
		ldpc_free(ldpc);
		return 1;
	}
*/

	// create all parity data at once
	rv = ldpc_encoder_create(ldpc, parity_buf, source_count, parity_count);
	if (rv != 0){
		printf("Failed: ldpc_encoder_create, %d\n", rv);
		free(source_buf);
		free(parity_buf);
		ldpc_free(ldpc);
		return 1;
	}

/*
	// create all parity data in 2 times
	rv = ldpc_encoder_create(ldpc, parity_buf, source_count, parity_count / 2);
	if (rv != 0){
		printf("Failed: ldpc_encoder_create, %d\n", rv);
		free(source_buf);
		free(parity_buf);
		ldpc_free(ldpc);
		return 1;
	}
	rv = ldpc_encoder_create(ldpc, parity_buf + (size_t)each_size * (parity_count / 2),
			source_count + parity_count / 2, parity_count - parity_count / 2);
	if (rv != 0){
		printf("Failed: ldpc_encoder_create, %d\n", rv);
		free(source_buf);
		free(parity_buf);
		ldpc_free(ldpc);
		return 1;
	}
*/

/*
{
	FILE *fp;
	if (fopen_s(&fp, "sourceO1.bin", "wb") == 0){
		fwrite(source_buf, 1, source_size, fp);
		fclose(fp);
	}
	if (fopen_s(&fp, "parityO1.bin", "wb") == 0){
		fwrite(parity_buf, 1, parity_size, fp);
		fclose(fp);
	}
	if (fopen_s(&fp, "bufferO1.bin", "wb") == 0){
		fwrite(ldpc->buf, 1, ldpc->al_size * (source_count + parity_count), fp);
		fclose(fp);
	}
}
*/

	finish = clock();
	duration = (double)(finish - start) / CLOCKS_PER_SEC;
	printf(" ... %.3f seconds\n", duration);

	ldpc_free(ldpc);	// No need encoder

	output_size = (size_t)each_size * parity_count;
	value_mb = (double)source_size / 1000000.f;
	input_speed = value_mb / duration;
	output_speed = (double)output_size / 1000000.f / duration;
	printf("LDPC-SC Encoder(%g MB in %d pieces, %d parities): Input= %g MB/s, Output= %g MB/s\n",
			value_mb, source_count, parity_count, input_speed, output_speed);

	start = clock();
	printf("\n set %d random error and allocate memory ...\n", lost_count);

	// shuffle order of source and parity data
	malloc_size = (size_t)(source_count + parity_count) * sizeof(int);	// available source and parity
	order_buf = malloc(malloc_size);
	if (order_buf == NULL){
		printf("Failed: malloc, %zd\n", malloc_size);
		free(source_buf);
		free(parity_buf);
		return 1;
	}
	max = source_count + parity_count;
	for (i = 0; i < max; i++)
		order_buf[i] = i;

	// swap index to select available data
	pcg32_srandom(0, 1);
	// Fisher–Yates shuffle [0 ~ source_count - 1]
	i = source_count - 1;
	while (i > 0){
		j = pcg32_boundedrand(i + 1);
		rv = order_buf[j];
		order_buf[j] = order_buf[i];
		order_buf[i] = rv;
		i--;
	}
	// shuffle [source_count ~ source_count + lost_count - 1]
	min = source_count;
	i = source_count + lost_count - 1;
	while (i > min){
		j = min + pcg32_boundedrand(i - min + 1);
		rv = order_buf[j];
		order_buf[j] = order_buf[i];
		order_buf[i] = rv;
		i--;
	}
	if (parity_count > lost_count){
		// shuffle [source_count + lost_count ~ source_count + parity_count - 1]
		min = source_count + lost_count;
		i = source_count + parity_count - 1;
		while (i > min){
			j = min + pcg32_boundedrand(i - min + 1);
			rv = order_buf[j];
			order_buf[j] = order_buf[i];
			order_buf[i] = rv;
			i--;
		}
	}
/*
printf("\norder list:\n");
for (i = source_count; i < source_count + parity_count; i++){
	printf(" %d", order_buf[i]);
}
printf("\n");
*/

	// allocate working buffer
	work_buf = calloc(source_size, 1);
	if (parity_buf == NULL){
		printf("Failed: malloc, %zd\n", source_size);
		free(source_buf);
		free(parity_buf);
		free(order_buf);
		return 1;
	}
	value_kb = (double)source_size / 1000;
	value_mb = (double)source_size / 1000000.f;
	printf("work data size = %zd bytes = %g KB = %g MB\n", source_size, value_kb, value_mb);

	finish = clock();
	duration = (double)(finish - start) / CLOCKS_PER_SEC;
	printf(" ... %.3f seconds\n", duration);

	printf("\n decode ...\n");
	start = clock();

	// initialize LDPC-Staircase decoder
	ldpc = ldpc_decoder_new(source_count, parity_count, each_size);
	if (ldpc == NULL){
		printf("Failed: ldpc_decoder_new\n");
		free(source_buf);
		free(parity_buf);
		free(order_buf);
		free(work_buf);
		return 1;
	}

	// It may require 2% overhead at least 2.
	ok_count = source_count + ldpc_decoder_overhead(ldpc);
	printf("It may require %u valid data to recover lost data.\n", ok_count);

	// input available source data at first
	ok_count = 0;
	valid_count = 0;
	ignore_count = 0;
	max = source_count - lost_count;
	for (i = 0; i < max; i++){
		id = order_buf[i];	// index of available source data
		rv = ldpc_decoder_add(ldpc, source_buf + (size_t)each_size * id, id);
		if (rv < 0){
			printf("Failed: ldpc_decoder_add, %d, ID = %d\n", rv, id);
			free(source_buf);
			free(parity_buf);
			free(order_buf);
			free(work_buf);
			ldpc_free(ldpc);
			return 1;
		}
		if (rv == LDPC_ADD_IGN){
			ignore_count++;
		} else {
			valid_count++;
		}
	}
	printf("input %d available source data (%u valid), ignored %d times\n", max, valid_count, ignore_count);

	input_count = 0;
	// input available parity data until possible repair
	for (i = 0; i < parity_count - parity_lost; i++){
		input_count++;
		id = order_buf[source_count + i];	// index of available parity data
		rv = ldpc_decoder_add(ldpc, parity_buf + (size_t)each_size * (id - source_count), id);
		if (rv < 0){
			printf("Failed: ldpc_decoder_add, %d, ID = %d\n", rv, id);
			free(source_buf);
			free(parity_buf);
			free(order_buf);
			free(work_buf);
			ldpc_free(ldpc);
			return 1;
		}
		if (rv == LDPC_ADD_IGN){
			ignore_count++;
		} else {
			valid_count++;
			if (rv == LDPC_ADD_END)	// got enough data to repair
				break;
		}

/*
		// try to recover with half possible symbols
		if (((parity_count - parity_lost + lost_count) / 2 == i + 1) && (i + 2 < parity_count - parity_lost)){

			// Iterative decoding may be enough while adding symbols.
			//rv = ldpc_decoder_solve_iterative(ldpc);

			rv = ldpc_decoder_solve_hybrid(ldpc, 5000);

			if (rv < 0){
				printf("Failed: ldpc_decoder_solve, %d\n", rv);
				free(source_buf);
				free(parity_buf);
				free(order_buf);
				free(work_buf);
				ldpc_free(ldpc);
				return 1;
			}
			if (rv == LDPC_ENC_OK){	// got enough data to repair
				ok_count = valid_count;	// recovered successfully in this count
				break;
			}
		}
*/
	}
	printf("input %d available parity data (%u valid), ignored %d times\n", input_count, valid_count, ignore_count);

/*
{
	FILE *fp;
	if (fopen_s(&fp, "bufferO2.bin", "wb") == 0){
		fwrite(ldpc->buf, 1, ldpc->al_size * (source_count + parity_count), fp);
		fclose(fp);
	}
}
*/

	if (ok_count == 0){	// not recovered yet

		// This may require large overhead.
		//rv = ldpc_decoder_solve_iterative(ldpc);

		// Because gaussian elimination is very slow, don't try for many symbols.
		rv = ldpc_decoder_solve_hybrid(ldpc, 10000);

		if (rv != 0){
			printf("Failed: ldpc_decoder_solve, %d\n", rv);
			free(source_buf);
			free(parity_buf);
			free(order_buf);
			free(work_buf);
			ldpc_free(ldpc);
			return 1;
		}
	}

/*
{
	FILE *fp;
	if (fopen_s(&fp, "bufferO3.bin", "wb") == 0){
		fwrite(ldpc->buf, 1, ldpc->al_size * (source_count + parity_count), fp);
		fclose(fp);
	}
}
*/

/*
	// recover all lost source data at once
	rv = ldpc_decoder_recover(ldpc, work_buf, 0, source_count);
	if (rv != 0){
		printf("Failed: ldpc_decoder_recover, %d\n", rv);
		free(source_buf);
		free(parity_buf);
		free(order_buf);
		free(work_buf);
		ldpc_free(ldpc);
		return 1;
	}
*/

	// recover every lost source data one by one
	for (i = 0; i < lost_count; i++){
		id = order_buf[i + source_count - lost_count];	// index of lost source data
		rv = ldpc_decoder_recover(ldpc, work_buf + (size_t)each_size * id, id, 1);
		if (rv != 0){
			printf("Failed: ldpc_decoder_recover, %d, ID = %d\n", rv, id);
			free(source_buf);
			free(parity_buf);
			free(order_buf);
			free(work_buf);
			ldpc_free(ldpc);
			return 1;
		}
	}

	finish = clock();
	duration = (double)(finish - start) / CLOCKS_PER_SEC;
	printf(" ... %.3f seconds\n", duration);

	free(source_buf);
	free(parity_buf);
	ldpc_free(ldpc);	// No need decoder

	output_size = (size_t)each_size * lost_count;	// bytes
	value_kb = (double)output_size / 1000;
	value_mb = (double)output_size / 1000000.f;
	printf("lost data size = %zd bytes = %g KB = %g MB\n", output_size, value_kb, value_mb);
	output_speed = value_mb / duration;

	input_count += source_count - lost_count;
	input_size = (size_t)each_size * input_count;
	value_kb = (double)input_size / 1000;
	value_mb = (double)input_size / 1000000.f;
	printf("input data size = %zd bytes = %g KB = %g MB\n", input_size, value_kb, value_mb);
	input_speed = value_mb / duration;
	printf("LDPC-SC Decoder(%g MB in %d pieces, %d losses): Input= %g MB/s, Output= %g MB/s\n",
			value_mb, input_count, lost_count, input_speed, output_speed);

	start = clock();
	printf("\n verify recovered data ...\n");

	//lost_count = source_count;	// If you want to check all source data, enable this line.
	max = each_size / 4;
	for (i = 0; i < lost_count; i++){
		id = order_buf[i + source_count - lost_count];	// index of lost source data
		int_p = (int *)(work_buf + (size_t)each_size * id);
		pcg32_srandom(0, id);
		for (j = 0; j < max; j++){
			if (int_p[j] != pcg32_random())
				break;
		}
		if (each_size & 3){	// each_size may not be multiple of 4.
			rv = pcg32_random();
			if (each_size % 4 == 1){
				if (work_buf[(size_t)each_size * id + (each_size & ~3)] != (char)rv)
					j = -1;
			} else if (each_size % 4 == 2){
				if (work_buf[(size_t)each_size * id + (each_size & ~3)] != (char)rv){
					j = -1;
				} else if (work_buf[(size_t)each_size * id + (each_size & ~3) + 1] != (char)(rv >> 8)){
					j = -2;
				}
			} else if (each_size % 4 == 3){
				if (work_buf[(size_t)each_size * id + (each_size & ~3)] != (char)rv){
					j = -1;
				} else if (work_buf[(size_t)each_size * id + (each_size & ~3) + 1] != (char)(rv >> 8)){
					j = -2;
				} else if (work_buf[(size_t)each_size * id + (each_size & ~3) + 2] != (char)(rv >> 16)){
					j = -3;
				}
			}
		}
		if (j < max){
			printf("Failed: recovered data, %d, ID = %d\n", i, id);
			break;
		}
	}
	if (i == lost_count)
		printf("All data repaired !\n");

	free(order_buf);	// End comparison
	free(work_buf);

	finish = clock();
	duration = (double)(finish - start) / CLOCKS_PER_SEC;
	printf(" ... %.3f seconds\n", duration);

	return 0;
}

