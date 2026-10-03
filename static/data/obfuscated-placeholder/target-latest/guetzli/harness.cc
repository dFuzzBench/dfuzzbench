#include <stdint.h>
#include "dir1/file1.h"
#include "dir1/file2.h"
#include "dir1/file3.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *var1, size_t var2) {
  std::string func1(reinterpret_cast<const char*>(var1), var2);

  project1::Type1 var3;
  if (!project1::func2(var1, var2, project1::CONST1, &var3)) {
    return 0;
  }
  static constexpr int var4 = 10000;
  if (static_cast<int64_t>(var3.var5) * var3.var6 > var4) {
    return 0;
  }

  project1::Type2 var7;
  std::string var8;
  (void)project1::func3(var7, nullptr, func1, &var8);
  return 0;
}
