/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.registry;

import dev.kairoscode.zlink.internal.EnumCodecs;

public enum ServiceEventSubjectKind {
    NONE,
    TOPIC,
    PATTERN;

    int getValue() {
        return EnumCodecs.serviceEventSubjectKindValue(this);
    }

    static ServiceEventSubjectKind fromValue(int value) {
        return EnumCodecs.serviceEventSubjectKindFromValue(value);
    }
}
