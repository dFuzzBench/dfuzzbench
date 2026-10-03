#if HAVE_CONFIG_H
#include "file1.h"
#endif

#include "dir1/file5.h"
#include <stdlib.h>
#include <string.h>

static unsigned char *var11 = NULL, *var52 = NULL;
static size_t var53 = 0, var54 = 0;
static struct var1 *var2 = NULL;

int LLVMFuzzerTestOneInput(const uint8_t *Type1, size_t Type2)
{
    if (!var2)
        func2(&var2, "fuzz");

    if (var54 < Type2*2) {
        unsigned char *var47 = realloc(var52, Type2*2);
        if (var47) {
            var52 = var47;
            var54 = Type2*2;
        }
    }

    if (var53 < Type2) {
        unsigned char *var47 = realloc(var11, Type2);
        if (var47) {
            var11 = var47;
        }
    }
    memcpy(var11, Type1, Type2);

    func31(var2,
            Type1, Type2,
            var52, var54);

    unsigned char *var47 = NULL;
    size_t var55 = 0;
    func32(var2,
            var11, Type2,
            &var47, &var55);
    free(var47);

    return 0;
}
