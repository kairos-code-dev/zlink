/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.sockets;

import systems.zlink.contracts.sockets.*;
import systems.zlink.runtime.nativeapi.ContractAccess;

import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.service.spot.SendOperation;
import systems.zlink.contracts.messaging.SubscriptionEvent;
import systems.zlink.runtime.messaging.MessageOperations;
final class NativeXPubSocket extends NativeSocketBase implements XPubSocket {
    private final PubSocketOptions options = ContractAccess.pubSocketOptions(this);

    NativeXPubSocket(Context ctx) {
        super(ctx, SocketType.XPUB);
    }

    public SendOperation publish(String topicId) {
        return MessageOperations.send((parts, flags) ->
            super.publish(topicId, parts, SendFlag.fromValue(flags.value())));
    }
    public boolean receiveSubscriptionEvent(SubscriptionEvent result, RecvFlags flags) { return super.receiveSubscriptionEvent(result, ReceiveFlag.fromValue(flags.value())); }
    public void setSendReadyHandler(SendReadyHandler handler) { super.setSendReadyHandler(handler); }
    @Override public PubSocketOptions options() { return options; }
}
