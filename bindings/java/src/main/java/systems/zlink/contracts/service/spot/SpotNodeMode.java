/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;


import systems.zlink.runtime.nativeapi.EnumCodecs;

public enum SpotNodeMode {
    PUBSUB,
    ROUTED,
    ALL;

    int getValue() {
        return EnumCodecs.spotNodeModeValue(this);
    }
}
