#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <memory>

#include "file1.h"
#include "file2.h"
#include "file3.h"
#include "file4.h"
#include "file5.h"
#include "file6.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *var1, size_t var2)
{
  int32_t var3;
  int32_t var4 = 0;
  int32_t var5;
  int var6 = (int) CONST1;
  int32_t var7;
  Type1 *var8;
  Type2 var9 = {0};
  Type3 var10;
  std::unique_ptr<uint8_t[]> func1(new uint8_t[var2 + 4]);
  uint8_t* var11[3] = {NULL};
  uint8_t var12[4] = {0, 0, 0, 1};

  memcpy(func1.get(), var1, var2);
  memcpy(func1.get() + var2, &var12[0], 4);
  memset(&var10, 0, sizeof(Type3));

  var9.var13 = CONST2;
  var9.var14.var15 = CONST3;

  func2 (&var8);
  var8->func3 (&var9);
  var8->func4 (CONST4, &var6);

  while (1) {
    if (var4 >= var2) {
      var5 = 1;
      if (var5)
        var8->func4 (CONST5, (void*)&var5);
      break;
    }

    for (var3 = 0; var3 < var2; var3++) {
      if ((func1[var4 + var3] == 0 && func1[var4 + var3 + 1] == 0 && func1[var4 + var3 + 2] == 0 && func1[var4 + var3 + 3] == 1
          && var3 > 0) || (func1[var4 + var3] == 0 && func1[var4 + var3 + 1] == 0 && func1[var4 + var3 + 2] == 1 && var3 > 0)) {
        break;
      }
    }
    var7 = var3;
    if (var7 < 4) {
      if (var7 == 0) {
        goto var16;
      }
      var4 += var7;
      continue;
    }

    var8->func5 (func1.get() + var4, var7, var11, &var10);
    var4 += var7;
  }

var16:
  var8->func6 ();
  func7 (var8);

  return 0;
}
