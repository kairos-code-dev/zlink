/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.core;

public final class ZlinkVersion {
    private ZlinkVersion() {}

    public static int[] get() {
        return Zlink.version();
    }
}
