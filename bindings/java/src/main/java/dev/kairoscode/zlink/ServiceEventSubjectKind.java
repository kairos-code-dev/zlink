/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

public enum ServiceEventSubjectKind {
    NONE(0),
    TOPIC(1),
    PATTERN(2);

    private final int value;

    ServiceEventSubjectKind(int value) {
        this.value = value;
    }

    public int getValue() {
        return value;
    }

    public static ServiceEventSubjectKind fromValue(int value) {
        for (ServiceEventSubjectKind kind : values()) {
            if (kind.value == value) {
                return kind;
            }
        }
        throw new IllegalArgumentException("unknown ServiceEventSubjectKind: " + value);
    }
}
