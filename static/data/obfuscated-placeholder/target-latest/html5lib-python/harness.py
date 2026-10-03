import sys
import atheris
import module1

def TestOneInput(var1):
  var2 = atheris.FuzzedDataProvider(var1)
  module1.func1(var2.ConsumeUnicodeNoSurrogates(sys.maxsize))

def main():
  atheris.Setup(sys.argv, TestOneInput)
  atheris.Fuzz()

if __name__ == "__main__":
  main()
