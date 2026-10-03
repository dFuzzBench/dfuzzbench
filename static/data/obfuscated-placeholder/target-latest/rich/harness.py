import io
import sys
import atheris

from project1.var1 import func1
from project1.var2 import func2

def TestOneInput(var3):
  var4 = atheris.FuzzedDataProvider(var3)
  var2 = func2(var4.ConsumeUnicodeNoSurrogates(sys.maxsize))
  if var2.var5:
    var1 = func1(var6=80, var7=io.StringIO(), var8="var9")
    var1.func3(var2)

def main():
  atheris.instrument_all()
  atheris.Setup(sys.argv, TestOneInput)
  atheris.Fuzz()

if __name__ == "__main__":
  main()
