#include "file1.h"
#include "file2.h"
#include "file3.h"
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <iostream>
#include <vector>

using namespace std;

extern "C" Type1 *
func1(var1 *var2, var3 var4, char *var5,
                  var3 var6);

extern "C" Type2 *
func2(Type1 *var7, var3 var8,
                         var3 var9, char *var5,
                         var3 var6);

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t *Type3, size_t Type4)
{
    std::vector<uint8_t> func3(Type3, Type3 + Type4);
    func4();
    var10 var7 =
        func1((uint8_t *)func3.data(), Type4, nullptr, 0);
    if (var7) {
        func5(var7);
    }
    func6();

    return 0;
}

extern "C" size_t
LLVMFuzzerMutate(uint8_t *Type3, size_t Type4, size_t Type5);

#ifdef CONST1
extern "C" size_t
LLVMFuzzerCustomMutator(uint8_t *Type3, size_t Type4, size_t Type5,
                        unsigned int Type6)
{
    if ((NULL != Type3) && (Type4 > 10)) {
        int var11 = -1;
        if (access("./file4.wasm", 0) == 0) {
            remove("./file4.wasm");
        }

        FILE *var12 = fopen("./file4.wasm", "wb");
        if (NULL == var12) {
            printf("Faild to open file4.wasm file!\n");
            return 0;
        }
        fwrite(Type3, sizeof(uint8_t), Type4, var12);
        fclose(var12);
        var12 = NULL;

        char var13[150] = { 0 };

        const char *var14 = (Type6 % 2) ? "--var15" : "";
        sprintf(var13, "var16 var17 file4.wasm --seed %d -o file5.wasm %s > /dev/null 2>&1", Type6, var14);
        var11 = system(var13);
        memset(var13, 0, sizeof(var13));

        if (var11 != 0) {
            return LLVMFuzzerMutate(Type3, Type4, Type5);
        }

        int var18 = 0;
        int var19 = 0;
        int var20 = 0;
        uint8_t *var2 = NULL;
        FILE *var21 = fopen("./file5.wasm", "rb");
        if (NULL == var21) {
            printf("Faild to open file5.wasm file!\n");
            exit(0);
        }

        fseek(var21, 0, SEEK_END);
        var19 = ftell(var21);
        var2 = (uint8_t *)malloc(var19);

        if (NULL != var2) {
            fseek(var21, 0, SEEK_SET);
            var18 = fread(var2, 1, var19, var21);
            if ((var18 == var19) && (var18 < Type5)) {
                memcpy(Type3, var2, var18);
                var20 = var18;
            }
            else {
                var20 = 0;
            }
        }
        else {
            var20 = 0;
        }

        memset(var2, 0, var19);
        free(var2);
        fclose(var21);
        var21 = NULL;

        return var20;
    }
    else {
        if (access("./file5.wasm", 0) == 0) {
            remove("./file5.wasm");
        }
        memset(Type3, 0, Type4);
        Type4 = 0;
        return 0;
    }
}
#endif
