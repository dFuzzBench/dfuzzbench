import sys
import atheris
import project1

def TestOneInput(var1):
  var2 = atheris.FuzzedDataProvider(var1)
  var3 = var2.ConsumeUnicode(atheris.ALL_REMAINING)

  project1.func1(var3)

def main():
  atheris.Setup(sys.argv, TestOneInput, enable_python_coverage=True)
  atheris.Fuzz()

if __name__ == "__main__":
  main()
