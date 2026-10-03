#include <dir1/file1.h>
#include <dir4/file4.h>
#include <stddef.h>
#include <stdint.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* var1, size_t var2)
{
    auto var5 = CONST1::func1(static_cast<const unsigned char*>(var1), var2);
    Type3::func5 func6(var5);
    func6.func7();
    return 0;
}
