/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink;

import systems.zlink.internal.Native;

final class ZlinkVersion {
    private ZlinkVersion() {}

    public static int[] get() {
        return Native.version();
    }
}
