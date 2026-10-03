#include <dir1/file1.h>
#include <dir2/file2.h>
#include <stddef.h>
#include <stdint.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* var1, size_t var2)
{
    auto var3 = CONST1::func1(static_cast<const unsigned char*>(var1), var2);
    Type1<Type2> func2(var3);
    (void)func2;
    return 0;
}
