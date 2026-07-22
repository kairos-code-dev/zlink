/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

/** The kind of a spot. */
public enum SpotKind {
    /** No spot kind (unset). */
    INVALID,
    /** A node's well-known entry spot. */
    ENTRY,
    /** A user-created spot. */
    USER
}
