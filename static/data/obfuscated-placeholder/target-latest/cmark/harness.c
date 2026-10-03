#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "file1.h"

int LLVMFuzzerTestOneInput(const uint8_t* var1, size_t var2) {
  struct __attribute__((packed)) {
    int var3;
    int var4;
  } var5;

  if (var2 >= sizeof(var5)) {
    memcpy(&var5, var1, sizeof(var5));
    int var3 = var5.var3;

    var3 &= (CONST1 | CONST2 | CONST3 | CONST4 | CONST5 | CONST6 | CONST7);

    const char *var6 = (const char *)(var1 + sizeof(var5));
    size_t var7 = var2 - sizeof(var5);
    var8 *var9 = NULL;

    switch (((unsigned) var5.var3 >> 30) & 3) {
      case 0:
        var9 = func1(var6, var7, var3);
        break;

      case 1:
        if (var7 > 0) {
          FILE *var10 = fmemopen((void *) var6, var7, "r");
          var9 = func2(var10, var3);
          fclose(var10);
        }
        break;

      case 2: {
        size_t var11 = 20;
        var12 *var13 = func3(var3);

        while (var7 > 0) {
          size_t var14 = var7 > var11 ? var11 : var7;
          func4(var13, var6, var14);
          var6 += var14;
          var7 -= var14;
        }

        var9 = func5(var13);
        func6(var13);
        break;
      }

      case 3:
        free(func7(var6, var7, var3));
        break;
    }

    if (var9 != NULL) {
      free(func8(var9, var3, var5.var4));
      free(func9(var9, var3));
      free(func10(var9, var3, var5.var4));
      free(func11(var9, var3, var5.var4));
      free(func12(var9, var3));

      func13(var9);
    }
  }
  return 0;
}
