#include <zlink/common.h>

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr))                                                                               \
            return __LINE__;                                                                       \
    } while (0)

int main (void)
{
    CHECK (ZLINK_VERSION_MAJOR == 8);
    CHECK (ZLINK_VERSION_MINOR == 4);
    CHECK (ZLINK_VERSION_PATCH == 3);
    CHECK (ZLINK_VERSION == ZLINK_MAKE_VERSION (8, 4, 0));
    return 0;
}
