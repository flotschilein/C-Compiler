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

#embed "test.cpp"
const char* my_file = {
#embed "test.cpp"
};

#define LOG(msg, ...) printf(msg __VA_OPT__(,) __VA_ARGS__)
#define STR(x) #x
#define CONCAT(a, b) a ## b

int main() {
    const char* f = __FILE__;
    int l = __LINE__;
    const char* combined = "Hello " "World";
    return 0;
}
