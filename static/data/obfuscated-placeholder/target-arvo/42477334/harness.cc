#if HAVE_CONFIG_H
#include "file1.h"
#endif

#include "dir1/file3.h"
#include "dir1/file6.h"
#include <stdlib.h>
#include <string.h>

static struct var1 *var2 = NULL;
static struct var5 *var6 = NULL;
static var38 var4 = {0};

int LLVMFuzzerTestOneInput(const uint8_t *Type1, size_t Type2)
{
    int (* var39[])(struct var5 *, struct var8 *,
            const var17 **var40, size_t *var41) = {
        func22, var42,
        var43, var44,
        var45, var46
    };
    size_t var19;

    if (!var2)
        func2(&var2, "fuzz");
    if (!var6) {
        var4.var2 = var2;
        var6 = func23();
        if (var6) {
            var6->var4 = &var4;
        }
    }

    for (var19 = 0; var19 < sizeof var39/sizeof *var39; var19++) {
        struct var8 *var9;
        const var17 *var47 = Type1;
        size_t var26 = Type2;
        var9 = calloc(1, sizeof *var9);
        while (CONST1 == var39[var19](var6, var9, &var47, &var26)) {
            func24(var9);
        }
        func24(var9);
    }

    struct var48 *var49 = calloc(1, sizeof *var49);
    func25(var2, var49, Type1, Type2);
    func26(var49);

    struct var50 *var51 = func27();
    func28(var2, var51, Type1, Type2);
    func29(var51);

    func30(Type1, Type2, var6);

    return 0;
}
