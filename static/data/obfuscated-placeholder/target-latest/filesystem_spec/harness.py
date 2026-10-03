import io
import sys
import atheris
import pathlib

from module1.var1 import var2
import module1
from module1.var3 import Type1

from var4 import *
from module2 import *
from module2.var5 import Type2

@atheris.instrument_func
def TestOneInput(var6):
  var7 = atheris.FuzzedDataProvider(var6)

  var8 = var7.ConsumeUnicodeNoSurrogates(124)

  var8 = var8 + "file1.txt"
  var9 = module1.func1("var2", var10=True)
  var11 = pathlib.Path(var8)
  try:
    var11.write_bytes(var6)
  except:
    try:
      var11.unlink()
    except:
      pass
    return

  try:
    var9.func2(var11, var8, var12="put", var13=0.5)
  except Type1:
    try:
      var11.unlink()
    except:
      pass
    return
  except (
    Type2,
    TypeError,
    AssertionError
  ) as var14:
    try:
      var11.unlink()
    except:
      pass
    return

  with var9.func3(var8) as var15:
    var15.read()

  try:
    var11.unlink()
  except:
    pass

def main():
  atheris.instrument_all()
  atheris.Setup(sys.argv, TestOneInput)
  atheris.Fuzz()

if __name__ == "__main__":
  main()
