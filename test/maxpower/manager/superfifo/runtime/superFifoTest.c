/*
 * tcpFifoTest.c
 *
 *  Created on: 16 May 2013
 *      Author: itay
 */


#include <MaxSLiCInterface.h>

#define _CONCAT(x, y) x ## y
#define CONCAT(x, y) _CONCAT(x, y)
#define INIT_NAME CONCAT(DESIGN_NAME, _init)

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

#define PACKED __attribute__((packed))

typedef struct PACKED configWord_s {
	uint64_t  base;
	uint64_t  wordCount;
} configWord_t;

typedef struct PACKED {
	uint64_t  data[4];
} single_entry_t;

#define MAX_DEPTH (128*1024*1024)
#define STEP 512

uint8_t compare(single_entry_t* outputData, uint64_t configBase, size_t len)
{
	uint8_t fail = 0;
	for (size_t entryIx=0; entryIx < len; entryIx++) {
		uint64_t *output = (uint64_t *)outputData[entryIx].data;
		size_t quadsPerEntry = sizeof(single_entry_t) / sizeof(uint64_t);

		uint64_t expected = (configBase + entryIx);
		if (expected != output[0]) {
			fail = 1;
			printf("[Entry: %zd, Quad: %zd] Mismatch: input 0x%lx, output 0x%lx\n", entryIx, 0L, expected, output[0]);
		}

		for (size_t q = 1; !fail && q < quadsPerEntry; q++) {
			if (0 != output[q]) {
				fail = 1;
				printf("[Entry: %zd, Quad: %zd] Mismatch: input 0x%lx, output 0x%lx\n", entryIx, q, 0L, output[q]);
			}
		}
	}

	return fail;
}

int main(int argc, char *argv[])
{
	(void) argc;
	(void) argv;

	assert((MAX_DEPTH % STEP) == 0);

	max_file_t *maxfile = INIT_NAME();
	if(!maxfile) {
		printf("Failed to init MAX file\n");
		return -1;
	}

	max_config_set_bool(MAX_CONFIG_PRINTF_TO_STDOUT, true);

	const char *device_name = "*";
	printf("Opening device: %s\n", device_name);

	max_engine_t *engine = max_load(maxfile, device_name);
	if(!engine) {
		printf("Failed to open Max device\n");
		exit(-1);
	}

	max_reset_engine(engine);

	/*
	 * If we don't run an empty action, no debug outputs will be generated.
	 */
	max_actions_t *action = max_actions_init(maxfile, NULL);
	max_run(engine, action);
	max_actions_free(action);


	void *configWordBuffer = NULL;
	posix_memalign(&configWordBuffer, 4096, STEP * sizeof(configWord_t));
	max_llstream_t *configWordStream = max_llstream_setup(engine, "configWord", STEP, sizeof(configWord_t), configWordBuffer);

	uint64_t configBase = 0;
	printf("Sending config word...\n");
	void *configWordSlot;
	while (max_llstream_write_acquire(configWordStream, 1, &configWordSlot) != 1);
	configWord_t *configWord = configWordSlot;
	configWord->wordCount = MAX_DEPTH;
	configWord->base = configBase;
	max_llstream_write(configWordStream, 1);

	uint8_t fail = 0;
	for(size_t i = 0; i < MAX_DEPTH / STEP; i++ ){

		single_entry_t *outputData = calloc(STEP, sizeof(single_entry_t));
		assert(outputData != NULL);

		printf("Streaming 'read_fifo'...\n"); fflush(stdout);
		action = max_actions_init(maxfile, NULL);
		max_queue_output(action, "read_fifo", outputData, sizeof(single_entry_t) * STEP);
		max_disable_reset(action);
		max_disable_validation(action);
		max_enable_partial_memory(action);
		max_run(engine, action);
		max_actions_free(action);

		printf("Comparing...\n"); fflush(stdout);
		size_t start = i * STEP;
		size_t base = configBase + start;
		fail |= compare(outputData, base, STEP);

		free(outputData);
		outputData = NULL;
	}
	

	printf("%s\n", fail ? "FAILED!" : "Success");
	return fail;
}
