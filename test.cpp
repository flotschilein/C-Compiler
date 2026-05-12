#define SQUARE(x) x * x
#define HELLO "Hello World"

#ifdef HELLO
int a = 1;
#endif

#ifndef WORLD
int b = 2;
#endif

#if 1
int c = SQUARE(5);
#endif

#if defined(HELLO) && 1
const char* s = HELLO;
#endif

#if __has_include("test.cpp")
int d = 42;
#endif

int main() {
    return 0;
}
