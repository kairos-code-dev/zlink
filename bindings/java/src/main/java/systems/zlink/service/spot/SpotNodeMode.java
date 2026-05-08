/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.service.spot;

import systems.zlink.internal.EnumCodecs;

public enum SpotNodeMode {
    PUBSUB,
    ROUTED,
    ALL;

    int getValue() {
        return EnumCodecs.spotNodeModeValue(this);
    }
}
