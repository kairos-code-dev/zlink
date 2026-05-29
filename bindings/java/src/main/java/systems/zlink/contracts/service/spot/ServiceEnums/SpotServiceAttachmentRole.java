/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;


public enum SpotServiceAttachmentRole {
    ROUTER(1),
    PUB(2),
    SUB(3);

    private final int value;

    SpotServiceAttachmentRole(int value) {
        this.value = value;
    }

    public int value() {
        return value;
    }

    public static SpotServiceAttachmentRole fromValue(int value) {
        for (SpotServiceAttachmentRole role : values()) {
            if (role.value == value) {
                return role;
            }
        }
        throw new IllegalArgumentException(
            "invalid SpotServiceAttachmentRole value: " + value);
    }
}
