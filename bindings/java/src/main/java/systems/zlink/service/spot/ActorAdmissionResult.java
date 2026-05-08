/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.service.spot;

import systems.zlink.internal.EnumCodecs;

public enum ActorAdmissionResult {
    ACCEPT,
    REJECT;

    int value() {
        return EnumCodecs.actorAdmissionResultValue(this);
    }
}
