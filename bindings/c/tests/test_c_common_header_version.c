#include <zlink/common.h>

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr))                                                                               \
            return __LINE__;                                                                       \
    } while (0)

int main (void)
{
    CHECK (ZLINK_VERSION_MAJOR == 7);
    CHECK (ZLINK_VERSION_MINOR == 0);
    CHECK (ZLINK_VERSION_PATCH == 0);
    CHECK (ZLINK_VERSION == ZLINK_MAKE_VERSION (7, 0, 0));
    return 0;
}
