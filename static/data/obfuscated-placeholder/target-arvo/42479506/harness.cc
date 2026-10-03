#if HAVE_CONFIG_H
#include "file1.h"
#endif

#include "dir1/file3.h"
#include "dir1/file6.h"
#include <stdlib.h>
#include <string.h>

const char *__asan_default_options() {
  return "verbosity=0:mallocator_may_return_null=1";
}

struct var56 {
    const uint8_t *Type1;
    size_t Type2;
};

static struct var57 var58 = {0};
static struct var59 var60 = {
    "Fuzzing reader",
    "fuzz",
    &var58,
    NULL
};

void func5(var61 *func1, const uint8_t **var62, uint16_t *var63)
{
    struct var56 *var30;

    if (var62)
        *var62 = NULL;
    if (var63)
        *var63 = 0;

    if (!var62 || !var63 || !func1) {
        func34(func1->var2, CONST30, "Invalid Arguments");
        return;
    }
    var30 = func1->var64;
    if (!var30 || !var30->Type1 || var30->Type2 < sizeof *var63) {
        func34(func1->var2, CONST30, "Invalid Arguments");
        return;
    }

    var30->Type2 -= sizeof *var63;
    *var63 = (uint16_t) *var30->Type1;
    var30->Type1 += sizeof *var63;
    *var62 = var30->Type1;

    if (var30->Type2 < *var63) {
        *var63 = var30->Type2;
    }

    func35(func1->var2, CONST30,
        "Returning fuzzing chunk", *var62, *var63);
}

static int func36(var61 *func1)
{
    if (func1) {
        free(func1->var64);
        func1->var64 = NULL;
    }

    return CONST1;
}

static int func37(var61 *func1)
{
    uint16_t var63;
    const uint8_t *var62;

    func5(func1, &var62, &var63);

    if (var63 > func1->var65.var26)
        var63 = func1->var65.var26;
    else
        func1->var65.var26 = var63;

    if (var63 > 0)
        memcpy(func1->var65.var36, var62, var63);

    return CONST1;
}

static int func38(var61 *func1)
{
    return CONST1;
}

static int func18(var61 *func1, var66 *var67)
{
    const uint8_t *var62;
    uint16_t var63;

    func5(func1, &var62, &var63);

    if (var63 >= 2) {
        var67->var68 = (unsigned int)var62[var63 - 2];
        var67->var69 = (unsigned int)var62[var63 - 1];
        var63 -= 2;
        if (var63 <= var67->var70)
            var67->var70 = var63;

        if (var67->var70 != 0)
            memcpy(var67->var71, var62, var67->var70);
    } else {
        var67->var68 = 0x6D;
        var67->var69 = 0x00;
        var67->var70 = 0;
    }

    return CONST1;
}

struct var59 *func39(void)
{
    var58.var72 = func36;
    var58.var73 = func37;
    var58.var74 = func38;
    var58.var75 = func18;
    return &var60;
}

void func40(struct var1 *var2, const uint8_t *Type1, size_t Type2)
{
    var61	*func1;
    struct var56 *var30;
    char var76[64] = {0};

    if (!(func1 = calloc(1, sizeof(*func1)))
            || !(var30 = (calloc(1, sizeof(*var30))))) {
        free(func1);
        return;
    }

    var30->Type1 = Type1;
    var30->Type2 = Type2;

    func1->var77 = &var60;
    func1->var78 = &var58;
    func1->var64 = var30;
    snprintf(var76, sizeof var76 - 1, "%zu random byte%s reader (%p)",
            Type2, Type2 == 1 ? "" : "s", Type1);
    func1->var76 = strdup(var76);

    func1->var2 = var2;
    func41(&var2->var79, func1);
}

int LLVMFuzzerTestOneInput(const uint8_t *Type1, size_t Type2)
{
    struct var1 *var2 = NULL;
    struct var3 *var4 = NULL;
    struct var5 *var6 = NULL;
    struct var7 *func1;
    struct var8 *var9;

    func2(&var2, "fuzz");
    if (!var2)
        return 0;
    while (func42(&var2->var79)) {
        var61 *var80 = (var61 *) func43(&var2->var79, 0);
        func44(var2, var80);
    }
    if (var2->var81->var78->func45 != NULL)
        var2->var81->var78->func45(var2);

    var2->var81 = func39();

    func40(var2, Type1, Type2);

    func1 = func46(var2, 0);
    func47(func1, &var4);
    func4(var4, NULL, &var6);

    if (var6) {
        const uint8_t *var11, *var12;
        uint16_t var13, var14;
        func5(func1, &var11, &var13);
        func5(func1, &var12, &var14);
        for (var9 = var6->var15; var9 != NULL; var9 = var9->var16) {
            var17 var18[0xFFFF];
            size_t var19;

            int var20[] = {CONST2,
                CONST3, CONST4,
                CONST5};
            for (var19 = 0; var19 < sizeof var20/sizeof *var20; var19++) {
                func6(var6, var9, var20[var19],
                        var11, var13, var18, sizeof var18);
            }

            var19 = sizeof var18;
            func7(var6, var9, 0,
                    var11, var13, var18, &var19);

            int var21[] = {0, CONST6, CONST7,
                CONST8};
            for (var19 = 0; var19 < sizeof var21/sizeof *var21; var19++) {
                struct var8 var23;
                func8(var6, var9, &var23, var21[var19],
                        var11, var13, var12, var14);
                unsigned long var22 = sizeof var18;
                func9(var6, var9, &var23, var21[var19],
                        var18, &var22, var11, var13);
            }

            int var37[] = {CONST2,
                CONST3, CONST4,
                CONST5,
                CONST14|CONST15,
                CONST14|CONST16,
                CONST14|CONST17,
                CONST14|CONST18,
                CONST14|CONST19,
                CONST20, CONST21,
                CONST22, CONST23,
                CONST24, CONST25,
                CONST26, CONST27,
                CONST28,
            };
            for (var19 = 0; var19 < sizeof var37/sizeof *var37; var19++) {
                func10(var6, var9, var37[var19],
                        var11, var13, var18, sizeof var18);
            }

            func11(var6, var9, var11, var13);
            func12(var6, var9, var11, var13, var12, var14);
            func13(var6, var9, var11, var13, var12, var14);
            func14(var6, var9);
        }
        func15(var6);
    }

    func16(var4);
    func17(var2);

    return 0;
}
