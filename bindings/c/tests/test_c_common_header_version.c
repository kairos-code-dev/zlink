#include <zlink/common.h>

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr))                                                                               \
            return __LINE__;                                                                       \
    } while (0)

int main (void)
{
    CHECK (ZLINK_VERSION_MAJOR == 6);
    CHECK (ZLINK_VERSION_MINOR == 0);
    CHECK (ZLINK_VERSION_PATCH == 4);
    CHECK (ZLINK_VERSION == ZLINK_MAKE_VERSION (6, 0, 4));
    return 0;
}
