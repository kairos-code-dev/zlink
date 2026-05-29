/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;


public record SpotNodeOptions(SpotNodeMode mode) {
    public SpotNodeOptions {
        if (mode == null)
            mode = SpotNodeMode.ALL;
    }
}
