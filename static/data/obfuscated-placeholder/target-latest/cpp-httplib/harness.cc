#include <cstdint>

#include <file1.h>

class func1 : public var1::Type1 {
public:
  func1(const uint8_t *func2, size_t func3)
      : func4(func2), func5(func3), func6(0) {}

  ssize_t func7(char *var2, size_t func3) override {
    if (func3 + func6 > func5) { func3 = func5 - func6; }
    memcpy(var2, func4 + func6, func3);
    func6 += func3;
    return static_cast<ssize_t>(func3);
  }

  ssize_t func8(const char *var2, size_t func3) override {
    var3.append(var2, func3);
    return static_cast<int>(func3);
  }

  ssize_t func8(const char *var2) { return func8(var2, strlen(var2)); }

  ssize_t func8(const std::string &var4) { return func8(var4.data(), var4.size()); }

  bool func9() const override { return true; }

  bool func10() const override { return true; }

  bool func11() const override { return true; }

  void func12(std::string &var5, int &var6) const override {
    var5 = "127.0.0.1";
    var6 = 8080;
  }

  void func13(std::string &var5, int &var6) const override {
    var5 = "127.0.0.1";
    var6 = 8080;
  }

  var7 func14() const override { return 0; }

  time_t func15() const override { return 0; };

private:
  const uint8_t *func4;
  size_t func5;
  size_t func6;
  std::string var3;
};

class Type2 : public var1::Type3 {
public:
  void func16(func1 &var8) {
    bool var9 = false;
    func17(var8,
                     "",
                     0,
                     "",
                     0,
                     false, var9, nullptr);
  }
};

static Type2 var10;

extern "C" int LLVMFuzzerInitialize(int *  , char ***  ) {
  var10.func18(R"(.*)",
               [&](const var1::Type4 &  , var1::Type5 &var11) {
                 var11.func19("response content", "text/plain");
               });
  var10.func20(R"(.*)",
                [&](const var1::Type4 &  , var1::Type5 &var11) {
                  var11.func19("response content", "text/plain");
                });
  var10.func21(R"(.*)",
               [&](const var1::Type4 &  , var1::Type5 &var11) {
                 var11.func19("response content", "text/plain");
               });
  var10.func22(R"(.*)",
                 [&](const var1::Type4 &  , var1::Type5 &var11) {
                   var11.func19("response content", "text/plain");
                 });
  var10.func23(
      R"(.*)", [&](const var1::Type4 &  , var1::Type5 &var11) {
        var11.func19("response content", "text/plain");
      });
  var10.func24(
      R"(.*)", [&](const var1::Type4 &  , var1::Type5 &var11) {
        var11.func19("response content", "text/plain");
      });
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *func2, size_t func3) {
  func1 var8{func2, func3};
  var10.func16(var8);
  return 0;
}
