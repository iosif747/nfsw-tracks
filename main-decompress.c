/*
MIT License

Copyright (c) 2024

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include "decompress.c"

#ifdef _WIN32
# include <io.h>
# include <fcntl.h>
# define SET_BINARY_MODE(handle) setmode(handle, O_BINARY)
#else
# define SET_BINARY_MODE(handle) ((void)0)
#endif

int glberr;

// Read the entirety of the given file into memory, returning a pointer to
// said memory. You will need to free the pointer. The size of the data is
// returned in the "outsize" parameter.
#define RACHUNKSIZE 65536
unsigned char *read_all(FILE *f, size_t *outsize) {
	// Initial allocation
	*outsize = 0;
	unsigned char *buffer = malloc(RACHUNKSIZE);
	if (buffer == NULL) {
		glberr = errno;
		return NULL;
	}
	
	unsigned char *tmp;
	
	// Continue until we're at eof (this works for stdin too)
	while (!feof(f)) {
		int readbytes = fread(buffer + *outsize, 1, RACHUNKSIZE, f);
		*outsize += readbytes;
		if (readbytes) { // Only realloc if we read something
			// To make code simpler, always keep a full empty chunk at the end
			tmp = realloc(buffer, *outsize + RACHUNKSIZE);
			if (tmp == NULL) {
				glberr = errno;
				free(buffer);
				return NULL;
			}
			buffer = tmp;
		}
	}
	// outsize is already correct at loop exit
	return buffer;
}

unsigned char * read_file(char * path,size_t * length) {
	FILE * file = fopen(path,"rb");
	if (file == NULL) {
		glberr = errno;
		goto fail;
	}
	fseek(file, 0, SEEK_END);
	size_t size = ftell(file);
	fseek(file, 0, SEEK_SET);
	unsigned char * data = malloc(size);
	if (data == NULL) {
		glberr = errno;
		goto fail_file;
	}
	if (fread(data,size,1,file) < 1) {
		glberr = errno;
		goto fail_data;
	}
	fclose(file);
	*length = size;
	return data;
	
	fail_data:;
	free(data);
	fail_file:;
	fclose(file);
	fail:;
	return NULL;
}

int file_create_from_data(char * path,unsigned char * data,size_t length) {
	FILE * file = fopen(path,"wb");
	if (file == NULL) {
		glberr = errno;
		goto fail;
	}
	if (fwrite(data, length, 1, file) < 1) {
		glberr = errno;
		goto fail_file;
	}
	fclose(file);
	return 0;
	
	fail_file:;
	fclose(file);
	fail:;
	return -1;
}

void write_little_endian(uint32_t nr, unsigned char * out, size_t bt) {
	for(int i = 0; i < bt; i++) {
		out[i] = (nr & 0xFF);
		nr >>= 8;
	}
}

void write_big_endian(uint32_t nr, unsigned char * out, size_t bt) {
	for(int i = 0; i < bt; i++) {
		out[i] = ((nr >> (8 * (bt - 1))) & 0xFF);
		nr <<= 8;
	}
}

int main(int argc, char* argv[]) {
	SET_BINARY_MODE(STDIN_FILENO);
	SET_BINARY_MODE(STDOUT_FILENO);
	if (argc != 3) {
		fprintf(stderr,"Syntax: %s <input> <output>\n",argv[0]);
		fprintf(stderr,"You may use \"-\" for stdin/stdout\n");
		return 1;
	}
	
	// INPUT
	unsigned char * input_data;
	size_t input_length;
	if (strcmp(argv[1],"-") == 0) { // stdin
		input_data = read_all(stdin,&input_length);
		if (input_data == NULL) {
			goto fail_mem;
		}
	} else { // file
		input_data = read_file(argv[1],&input_length);
		if (input_data == NULL) {
			goto fail_mem;
		}
	}
	
	uint32_t header_unc_length = *((uint32_t *)(input_data + 8));
	write_little_endian(header_unc_length,(unsigned char *)&header_unc_length,4);
	//uint32_t header_cmp_length = *((uint32_t *)(input_data + 12));
	//write_little_endian(header_cmp_length,&header_cmp_length,4);
	
	// OUTPUT
	unsigned char * output_data = malloc(header_unc_length);
	if (output_data == NULL) {
		glberr = errno;
		goto fail_mem;
	}
	
	ea_jdlz_decompress(input_data + 16,input_length - 16,output_data,header_unc_length);
	if (header_unc_length < 1) {
		goto fail_jdlz;
	}
	
	if (strcmp(argv[2],"-") == 0) {
		if (fwrite(output_data, header_unc_length, 1, stdout) < 1) {
			glberr = errno;
			goto fail_file;
		}
	} else {
		if (file_create_from_data(argv[2], output_data, header_unc_length) == -1) {
			goto fail_file;
		}
	}
	
	return 0;
	
	fail_mem:;
	fprintf(stderr,"Could not allocate memory: %s\n",strerror(glberr));
	return 2;
	
	fail_file:;
	fprintf(stderr,"Creating file failed: %s\n",strerror(glberr));
	return 3;
	
	fail_jdlz:;
	fprintf(stderr,"Interpreting JDLZ file failed. Invalid file?");
	return 4;
}
