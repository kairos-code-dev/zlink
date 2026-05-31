/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;


/**
 * Options for creating a spot node.
 * @param mode which messaging patterns to enable; defaults to {@link SpotNodeMode#ALL} if null
 */
public record SpotNodeOptions(SpotNodeMode mode) {
    public SpotNodeOptions {
        if (mode == null)
            mode = SpotNodeMode.ALL;
    }
}
