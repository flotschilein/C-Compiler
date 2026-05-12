#include <deque>
#include <stdio.h>
#include <iostream>
#include <vector>

int main(int ac, char* av[]) {
    static const int filecount = ac - 1;
    if (ac == 1) {
        std::cout << "usage: compiler <file>" << std::endl;
    return 1;
    }
    static const std::vector<std::string> preprocessed_files;
    for (int i = 1; i < ac; i++) {
        std::cout << "preprocessing " << av[i] << std::endl;
    }
    return 0;
}