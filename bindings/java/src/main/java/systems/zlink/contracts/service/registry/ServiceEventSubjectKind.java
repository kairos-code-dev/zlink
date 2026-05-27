/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;


import systems.zlink.runtime.nativeapi.EnumCodecs;

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
