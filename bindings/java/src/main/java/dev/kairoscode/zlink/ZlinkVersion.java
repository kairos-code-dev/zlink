/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;

public final class ZlinkVersion {
    private ZlinkVersion() {}

    public static int[] get() {
        return Native.version();
    }
}
