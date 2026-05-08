/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink;

import systems.zlink.SocketOptions;
import java.util.List;
import java.util.Optional;

public final class SubSocketOptions extends CommonSocketOptions {
    SubSocketOptions(Socket socket) {
        super(socket);
    }

    public int topicsCount() {
        return socket.getOption(SocketOptions.TOPICS_COUNT);
    }

    Optional<SubscriptionEntry> subscriptionAt(int index) {
        if (index < 0)
            throw new IndexOutOfBoundsException("subscription index " + index);
        List<SubscriptionEntry> entries = socket.subscriptions();
        return index < entries.size() ? Optional.of(entries.get(index))
          : Optional.empty();
    }
}
