#include "iostream"
#include "libxr.hpp"
#include "libxr_type.hpp"

int main() {
  int (*fun)(char, int, float) = [](char a, int b, float c) {
    std::cout << a << b << c << std::endl;
    return 0;
  };
  char tmp = 'c';
  auto cb = new LibXR::Callback(fun, tmp);
  cb->RunFromUser(1, 2.5);

  LibXR::TimestampUS a(10), b(20);
  auto c = a - b;
  return 0;
}