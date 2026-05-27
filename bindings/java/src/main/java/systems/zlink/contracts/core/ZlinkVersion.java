/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.core;


import systems.zlink.runtime.nativeapi.Native;

public final class ZlinkVersion {
    private ZlinkVersion() {}

    public static int[] get() {
        return Native.version();
    }
}
