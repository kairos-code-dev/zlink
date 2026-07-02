/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/common.h>

static_assert(ZLINK_VERSION_MAJOR == 8, "zlink/common.h major version must match package version");
static_assert(ZLINK_VERSION_MINOR == 4, "zlink/common.h minor version must match package version");
static_assert(ZLINK_VERSION_PATCH == 3, "zlink/common.h patch version must match package version");
static_assert(ZLINK_VERSION == ZLINK_MAKE_VERSION(8, 4, 2),
  "zlink/common.h aggregate version must match package version");

int main()
{
    return 0;
}
