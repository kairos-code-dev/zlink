/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

public enum ActorAdmissionResult {
    ACCEPT(1),
    REJECT(2);

    private final int value;

    ActorAdmissionResult(int value) {
        this.value = value;
    }

    public int value() {
        return value;
    }
}
