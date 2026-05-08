/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

record SpotNodeOptions(SpotNodeMode mode) {
    SpotNodeOptions {
        if (mode == null)
            mode = SpotNodeMode.ALL;
    }
}
