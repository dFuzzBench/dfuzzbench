#include <dir5/file9.h>
#include <stddef.h>
#include <stdint.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* var1, size_t var2)
{
    Type4::func13(var1, var2);
    return 0;
}
