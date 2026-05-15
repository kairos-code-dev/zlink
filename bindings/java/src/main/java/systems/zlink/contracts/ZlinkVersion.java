/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;


import systems.zlink.runtime.nativebridge.Native;

final class ZlinkVersion {
    private ZlinkVersion() {}

    public static int[] get() {
        return Native.version();
    }
}
