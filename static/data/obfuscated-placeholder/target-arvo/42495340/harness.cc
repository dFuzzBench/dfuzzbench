#include <dir1/file1.h>
#include <dir1/file2.hpp>
#include <dir1/file3.h>
#include <dir1/file4.h>

using namespace Type1;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *var1, size_t var2) {
	var3 var4 = func1(var5,NULL);
	func2(&var4);

    Type2 var6;
    const var7 *var8 = var6.func3(var1, var2,
        var9, nullptr );

    func4(&var4);

    return 0;
}
