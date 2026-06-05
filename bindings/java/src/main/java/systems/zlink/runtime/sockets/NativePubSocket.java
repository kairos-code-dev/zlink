/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.sockets;

import systems.zlink.internal.sockets.SendFlag;

import systems.zlink.contracts.sockets.*;
import systems.zlink.internal.ContractAccess;

import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.service.spot.SendOperation;
import systems.zlink.runtime.messaging.MessageOperations;

final class NativePubSocket extends NativeSocketBase implements PubSocket {
    private final PubSocketOptions options = ContractAccess.pubSocketOptions(this);

    NativePubSocket(Context ctx) {
        super(ctx, SocketType.PUB);
    }

    public SendOperation publish(String topicId) {
        return MessageOperations.send(
            (part, flags) -> super.publish(topicId, part,
                SendFlag.fromValue(flags.value())),
            (parts, flags) ->
            super.publish(topicId, parts, SendFlag.fromValue(flags.value())));
    }
    public void setSendReadyHandler(SendReadyHandler handler) { super.setSendReadyHandler(handler); }
    @Override public PubSocketOptions options() { return options; }
}
