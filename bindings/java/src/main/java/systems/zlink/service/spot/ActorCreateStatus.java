/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.service.spot;

import systems.zlink.internal.EnumCodecs;

public enum ActorCreateStatus {
    CREATED,
    EXISTING;

    int value() {
        return EnumCodecs.actorCreateStatusValue(this);
    }

    static ActorCreateStatus fromValue(int value) {
        return EnumCodecs.actorCreateStatusFromValue(value);
    }
}
