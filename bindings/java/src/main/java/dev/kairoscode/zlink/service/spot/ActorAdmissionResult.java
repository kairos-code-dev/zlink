/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

import dev.kairoscode.zlink.internal.EnumCodecs;

public enum ActorAdmissionResult {
    ACCEPT,
    REJECT;

    int value() {
        return EnumCodecs.actorAdmissionResultValue(this);
    }
}
