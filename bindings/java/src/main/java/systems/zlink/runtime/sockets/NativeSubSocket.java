/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.sockets;

import systems.zlink.contracts.internal.sockets.ReceiveFlag;

import systems.zlink.contracts.sockets.*;
import systems.zlink.contracts.internal.ContractAccess;

import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.messaging.SubscriptionEntry;
import systems.zlink.contracts.messaging.TopicMessage;
import java.util.Optional;

final class NativeSubSocket extends NativeSocketBase implements SubSocket {
    private final SubSocketOptions options = ContractAccess.subSocketOptions(this);

    NativeSubSocket(Context ctx) {
        super(ctx, SocketType.SUB);
    }

    public Optional<SubscriptionEntry> subscriptionAt(int index) {
        if (index < 0)
            throw new IndexOutOfBoundsException("subscription index " + index);
        var entries = super.subscriptions();
        return index < entries.size() ? Optional.of(entries.get(index))
          : Optional.empty();
    }
    public boolean subscribe(TopicMessage result, RecvFlags flags) { return super.subscribe(result, ReceiveFlag.fromValue(flags.value())); }
    @Override public SubSocketOptions options() { return options; }
}
