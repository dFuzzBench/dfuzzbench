#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#define CONST1
#include "file1.h"

#define CONST2 \
  if(var1.var2) \
  { \
    if (var1.var3) \
      func1(var1.var2, var1.var3); \
    if (var1.var4) \
      func2(&var1.var2, &var1.var5,\
        &var1.var4); \
    else if (var1.var5) \
      func2(&var1.var2, &var1.var5,\
        nullptr); \
    else \
      func2(&var1.var2, nullptr, nullptr); \
    var1.var2 = nullptr; \
    var1.var3 = nullptr; \
    var1.var5 = nullptr; \
    var1.var4 = nullptr; \
  }

struct func3 {
  const uint8_t* func4;
  size_t var6;
};

struct func5 {
  var7 var5 = nullptr;
  var8 var2 = nullptr;
  var7 var4 = nullptr;
  var9 var3 = nullptr;
  func3* var10 = nullptr;

  ~func5() {
    if (var3)
      func1(var2, var3);
    if (var4)
      func2(&var2, &var5, &var4);
    else if (var5)
      func2(&var2, &var5, nullptr);
    else
      func2(&var2, nullptr, nullptr);
    delete var10;
  }
};

void func6(var8 var2, var11 func4, size_t var12) {
  func3* var10 = static_cast<func3*>(func7(var2));
  if (var12 > var10->var6) {
    func8(var2, "read error");
  }
  memcpy(func4, var10->func4, var12);
  var10->var6 -= var12;
  var10->func4 += var12;
}

void* func9(var8, var13 var14) {
  if (var14 > 8000000)
    return nullptr;

  return malloc(var14);
}

void func10(var8, var9 var15) {
  return free(var15);
}

static const int var16 = 8;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* func4, size_t var14) {
  if (var14 < var16) {
    return 0;
  }

  std::vector<unsigned char> func11(func4, func4 + var14);
  if (func12(func11.data(), 0, var16)) {
    return 0;
  }

  func5 var1;
  var1.var2 = nullptr;
  var1.var3 = nullptr;
  var1.var5 = nullptr;
  var1.var4 = nullptr;

  var1.var2 = func13
    (CONST3, nullptr, nullptr, nullptr);
  if (!var1.var2) {
    return 0;
  }

  var1.var5 = func14(var1.var2);
  if (!var1.var5) {
    CONST2
    return 0;
  }

  var1.var4 = func14(var1.var2);
  if (!var1.var4) {
    CONST2
    return 0;
  }

  func15(var1.var2, nullptr, func9, func10);

  func16(var1.var2, CONST4, CONST4);
#ifdef CONST5
  func17(var1.var2, CONST5, CONST6);
#endif

  var1.var10 = new func3();
  var1.var10->func4 = func4 + var16;
  var1.var10->var6 = var14 - var16;
  func18(var1.var2, var1.var10, func6);
  func19(var1.var2, var16);

  if (setjmp(func20(var1.var2))) {
    CONST2
    return 0;
  }

  func21(var1.var2, var1.var5);

  if (setjmp(func20(var1.var2))) {
    CONST2
    return 0;
  }

  var17 var18, var19;
  int var20, var21, var22, var23;
  int var24;

  if (!func22(var1.var2, var1.var5, &var18,
                    &var19, &var20, &var21, &var22,
                    &var23, &var24)) {
    CONST2
    return 0;
  }

  if (var18 && var19 > 100000000 / var18) {
    CONST2
    return 0;
  }

  func23(var1.var2);
  func24(var1.var2);
  func25(var1.var2);
  func26(var1.var2);
  func27(var1.var2);

  int var25 = func28(var1.var2);

  func29(var1.var2, var1.var5);

  var1.var3 = func30(
      var1.var2, func31(var1.var2,
                                            var1.var5));

  for (int var26 = 0; var26 < var25; ++var26) {
    for (var17 var27 = 0; var27 < var19; ++var27) {
      func32(var1.var2,
                   static_cast<var11>(var1.var3), nullptr);
    }
  }

  func33(var1.var2, var1.var4);

  CONST2

#ifdef CONST7
  var28 var29;
  memset(&var29, 0, (sizeof var29));
  var29.var30 = CONST8;

  if (!func34(&var29, func4, var14)) {
    return 0;
  }

  var29.var31 = CONST9;
  std::vector<var32> func35(func36(var29));
  func37(&var29, NULL, func35.data(), 0, NULL);
#endif

  return 0;
}
