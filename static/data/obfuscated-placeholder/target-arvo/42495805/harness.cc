#include <stdint.h>
#include <stddef.h>

#include "file1.h"

#define func1(...) __builtin_trap()

int LLVMFuzzerTestOneInput(const uint8_t *var1, size_t var2)
{
    Type1 var3 = var4;

    if (var2 < 8 || var2 > 256*1024) {
        return 0;
    }

    Type2 var5 = func2 ();
    if (var5) {
        Type3 var6 = func3 (var5, 64*1024, NULL);
        if (var6) {
            Type4 var7 = NULL;
            var3 = func4 (var5, &var7, var1, var2);
            if (var7) {
                var3 = func5 (var6, var7);
                if (var3 == 0) {
                    Type5 var8 = NULL;
                    var3 = func6 (&var8, var6, "fib");
                    if (var8) {
                        func7 (var8, 10);
                    }
                } else {
                    func16 (var7);
                }
            }

            func8(var6);
        }
        func9(var5);
    }

    return 0;
}
