#include <dir1/file1.hpp>

#include <cassert>
#include <iomanip>
#include <iostream>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* var1, size_t var2) {
  Type1::Type2::func1(Type1::Type2::var3);

  Type1::Type3::func2();
  ::atexit(Type1::Type3::var4);

  try {
    Type1::Type4 func3(var1, var2);
    Type1::Type5::Type6 var5 = Type1::Type7::func4(func3.func5(), var2);
    assert(var5.get() != 0);

    var5->func6();
    for (auto& var6 : var5->func7()) {
      if (var6.func8().substr(0, 2) != "0x") {
        var6.func9();
        var6.func9(&var5->func7());
      }
    }
    for (auto& var6 : var5->func10()) {
      if (var6.func8().substr(0, 2) != "0x") {
        var6.func9();
        var6.func9(&var5->func7());
      }
    }
    for (auto& var6 : var5->func11()) {
      if (var6.func8().substr(0, 2) != "0x") {
        var6.func9();
        var6.func9(&var5->func7());
      }
    }

    std::ostringstream var7;
    var5->func12(var7, Type1::var8);
    var5->func12(var7, Type1::var9);
    var5->func12(var7, Type1::var10);
    var5->func12(var7, Type1::var11);
    var5->func12(var7, Type1::var12);
    var5->func12(var7, Type1::var13);

    var5->func13();

  } catch (...) {
  }

  return 0;
}
