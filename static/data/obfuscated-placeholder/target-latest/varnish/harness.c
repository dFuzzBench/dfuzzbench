#include "file1.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "dir1/file2.h"
#include "dir1/file3.h"
#include "dir1/file4.h"
#include "dir1/file5.h"

#include "file6.h"

int LLVMFuzzerTestOneInput(const uint8_t *, size_t);

struct Type1 *Type2;
volatile struct var1 *var2;

int
func1(struct var3 *var3, int var4, int var5, const void *var6,
    const char *var7, unsigned var8, const char *var9, ...)
{
	(void)var3;
	(void)var4;
	(void)var5;
	(void)var6;
	(void)var7;
	(void)var8;
	(void)var9;
	return (0);
}

void
func2(enum Type3 var10, var11 var12, const char *var9, ...)
{
	(void)var10;
	(void)var12;
	(void)var9;
}

void
func3(struct var13 *var14, enum Type3 var10, const char *var9, ...)
{
	(void)var14;
	(void)var10;
	(void)var9;
}

void
func4(struct var13 *var15, const char *var16, var17 var18, var17 *var19,
    var17 var20)
{
	(void)var15;
	(void)var16;
	(void)var18;
	(void)var19;
	(void)var20;
}

void
func5(enum Type3 var10, const char *var9, ...)
{

	(void)var10;
	(void)var9;
}

int
LLVMFuzzerTestOneInput(const uint8_t* var21, size_t var22)
{
	struct Type1 var23;
	struct var1 var24;
	struct var25 var26[1];
	struct var25 var27[1];
	struct var28 var29[1];
	struct var30 var31[1];
	struct var32 var32[1];
	struct var33 *var34;
	struct var3 *var3;
	var35 var36[CONST1 + 1];
	char var37[1024];

	if (var22 < 1)
		return (0);

	func6(var21);

	Type2 = &var23;
	var2 = &var24;

	memset(&var24, 0, sizeof(var24));
#define func7(var38, var39) (var38)[(var39) >> 3] |= (0x80 >> ((var39) & 7))
	if (var21[0] & 0x8f)
		func7(var24.var40, CONST2);
	if (var22 > 1 && var21[1] & 0x8f)
		func7(var24.var40, CONST3);
	if (var22 > 2 && var21[2] & 0x8f)
		func7(var24.var40, CONST4);
	if (var22 > 3 && var21[3] & 0x8f)
		func7(var24.var40, CONST5);
#undef func7

	func8(var32, "req", var37, sizeof var37);

	func9(var26, CONST6);
	var26->var36 = var36;
	var26->var36[CONST1].var38 = "/";
	var26->var32 = var32;

	func9(var27, CONST6);
	var27->var32 = var32;

	func9(var31, CONST7);

	func9(var29, CONST8);
	var29->var31 = var31;
	var29->var27 = var27;

	var34 = func10(var29, var26, NULL, NULL);
	func6(var34);
	func11(var34, (const char *)var21, var22);
	var3 = func12(var34);
	if (var3 != NULL)
		func13(&var3);
	func14(var32, 0);

	return (0);
}

#if defined(CONST9)
int
main(int var41, char **var42)
{
	ssize_t var43;
	char *var44;
	int var45;

	for (var45 = 1; var45 < var41; var45++) {
		var43 = 0;
		var44 = func15(NULL, var42[var45], &var43);
		func6(var44);
		LLVMFuzzerTestOneInput((uint8_t *)var44, var43);
		free(var44);
	}
}
#endif
