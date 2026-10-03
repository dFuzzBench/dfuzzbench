/*
 * AFL driver for libFuzzer harnesses.
 * Reads input from a file (or stdin) and calls LLVMFuzzerTestOneInput.
 * Compile: $CC $CFLAGS -c afl_driver.c -o afl_driver.o
 * Link:    $CXX $CXXFLAGS afl_driver.o harness.o lib.a -o fuzzer
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

extern int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int main(int argc, char **argv) {
    FILE *f = stdin;
    if (argc > 1) {
        f = fopen(argv[1], "rb");
        if (!f) {
            perror("fopen");
            return 1;
        }
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len < 0) len = 0;
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc(len);
    if (len > 0 && buf) {
        fread(buf, 1, len, f);
    }
    if (f != stdin) fclose(f);
    LLVMFuzzerTestOneInput(buf, (size_t)len);
    free(buf);
    return 0;
}
