/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;


import systems.zlink.runtime.nativebridge.EnumCodecs;

public enum SpotNodeMode {
    PUBSUB,
    ROUTED,
    ALL;

    int getValue() {
        return EnumCodecs.spotNodeModeValue(this);
    }
}
