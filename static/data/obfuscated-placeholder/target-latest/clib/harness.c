#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include "../../dir1/dir2/file1.h"

int LLVMFuzzerTestOneInput(const uint8_t *var1, size_t var2) {
    if(var2<3){
            return 0;
    }
    char var3[256];
    sprintf(var3, "file2.json");

    FILE *var4 = fopen(var3, "wb");
    if (!var4)
            return 0;
    fwrite(var1, var2, 1, var4);
    fclose(var4);

    var5 *var6 =
	    func1(var3, 0);
    if(var6) {
	    func2(var6);
    }
    unlink(var3);
    return 0;
}
