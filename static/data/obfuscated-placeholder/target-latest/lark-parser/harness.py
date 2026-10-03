import sys
import atheris
import module1

def TestOneInput(var1):
  var2 = atheris.FuzzedDataProvider(var1)
  var3 = var2.ConsumeUnicodeNoSurrogates(var2.ConsumeIntInRange(1,4096))
  var4 = var2.ConsumeUnicodeNoSurrogates(var2.ConsumeIntInRange(1,4096))

  try:
    var5 = module1.module1.func1(var3)
    var5.func2(var4)
  except(module1.var6.Type1,):
    pass

def main():
  atheris.Setup(sys.argv, TestOneInput)
  atheris.Fuzz()

if __name__ == "__main__":
  main()
