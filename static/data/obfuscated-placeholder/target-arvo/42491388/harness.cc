#include <dir1/file1.h>
#include <dir7/file10.h>
#include <dir7/file11.h>
#include <dir7/file5.h>
#include <dir7/dir8/file12.h>
#include <stddef.h>
#include <stdint.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* var1, size_t var2)
{
    auto var8 = CONST1::func1(static_cast<const unsigned char*>(var1), var2);
    auto var9 = CONST2::func4(var8);
    auto func6 = CONST2::func5(var9);
    auto var10 = func6.func15();
    if (!func6.func16()) {
        auto var11 = CONST2::CONST3::func17();
        auto var12 = CONST2::Type7::func17<CONST2::Type8>(*var11);
        var12->func18(var12->func19(), *var10);
    }
    return 0;
}
