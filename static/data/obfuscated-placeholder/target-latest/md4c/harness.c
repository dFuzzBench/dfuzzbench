#include <stdint.h>
#include <stdlib.h>
#include "file1.h"

static void
func1(const CONST1* var1, CONST2 var2, void* var3)
{
   return;
}

int
LLVMFuzzerTestOneInput(const uint8_t *var4, size_t var2)
{
    unsigned var5, var6;

    if(var2 < 2 * sizeof(unsigned)) {
        return 0;
    }
    var5 = ((unsigned*)var4)[0];
    var6 = ((unsigned*)var4)[1];
    var4 += 2 * sizeof(unsigned);
    var2 -= 2 * sizeof(unsigned);

    func2(var4, var2, func1, NULL, var5, var6);
    return 0;
}
