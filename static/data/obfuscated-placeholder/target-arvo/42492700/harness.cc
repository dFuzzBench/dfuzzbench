#include <dir5/file8.h>
#include <stdio.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* var1, size_t var2)
{
    Type4::func11(var1, var2);
    return 0;
}
