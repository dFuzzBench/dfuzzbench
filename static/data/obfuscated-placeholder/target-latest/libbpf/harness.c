#include "file1.h"

static int func1(enum var1 var2, const char *var3, va_list var4)
{
	return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *var5, size_t var6) {
	struct var7 *var8 = NULL;
	func2(var9, var10);
	int var11;

	func3(func1);

	var10.var12 = "fuzz-object";
	var8 = func4(var5, var6, &var10);
	var11 = func5(var8);
	if (var11)
		return 0;

	func6(var8);
	return 0;
}
