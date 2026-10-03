#if HAVE_CONFIG_H
#include "file1.h"
#endif

#include "dir1/file5.h"

int LLVMFuzzerTestOneInput(const uint8_t *Type1, size_t Type2) {
    fclose(stdout);
    func20(Type1, Type2);
    return 0;
}
