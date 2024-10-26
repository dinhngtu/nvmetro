#include <cstddef>
#include <cstdlib>
#include <algorithm>

int main() {
    auto f = new char[1000000];
    std::fill((volatile char *)f, (volatile char *)(f + 1000000), '\xfa');
    delete[] f;

    auto g = static_cast<char *>(malloc(2000000));
    std::fill((volatile char *)g, (volatile char *)(g + 2000000), '\xfc');
    free(g);

    return 0;
}
