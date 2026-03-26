/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import java.util.Arrays;

/** Snapshot entry returned from {@link Socket#subscriptions()}. */
public record SubscriptionEntry(byte[] filter, boolean pattern) {
    public SubscriptionEntry {
        filter = filter == null ? new byte[0] : Arrays.copyOf(filter, filter.length);
    }

    @Override
    public byte[] filter() {
        return Arrays.copyOf(filter, filter.length);
    }
}
