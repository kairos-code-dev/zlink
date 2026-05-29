/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;

import systems.zlink.internal.ContractAccess;

import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.core.RoutingId;
import java.util.Objects;

public class PubSocketOptions extends CommonSocketOptions {
    private boolean manualEnabled;

    public PubSocketOptions(Socket socket) {
        super(socket);
    }

    public boolean verbose() {
        return ContractAccess.socketGetOption(socket, SocketOptions.XPUB_VERBOSE) != 0;
    }

    public void verbose(boolean enabled) {
        ContractAccess.socketSetOption(socket, SocketOptions.XPUB_VERBOSE, enabled ? 1 : 0);
    }

    public boolean verboser() {
        return ContractAccess.socketGetOption(socket, SocketOptions.XPUB_VERBOSER) != 0;
    }

    public void verboser(boolean enabled) {
        ContractAccess.socketSetOption(socket, SocketOptions.XPUB_VERBOSER, enabled ? 1 : 0);
    }

    public boolean noDrop() {
        return ContractAccess.socketGetOption(socket, SocketOptions.XPUB_NODROP) != 0;
    }

    public void noDrop(boolean enabled) {
        ContractAccess.socketSetOption(socket, SocketOptions.XPUB_NODROP, enabled ? 1 : 0);
    }

    public boolean manual() {
        return manualEnabled;
    }

    public void manual(boolean enabled) {
        manualEnabled = enabled;
        ContractAccess.socketSetOption(socket, SocketOptions.XPUB_MANUAL, enabled ? 1 : 0);
    }

    public boolean manualLastValue() {
        return ContractAccess.socketGetOption(socket, SocketOptions.XPUB_MANUAL_LAST_VALUE) != 0;
    }

    public void manualLastValue(boolean enabled) {
        ContractAccess.socketSetOption(socket, SocketOptions.XPUB_MANUAL_LAST_VALUE, enabled ? 1 : 0);
    }

    public void approveSubscribe(RoutingId routingId) {
        setSubscribeDecision(SocketOptions.PUB_APPROVE_SUBSCRIBE_BYTES, routingId);
    }

    public void rejectSubscribe(RoutingId routingId) {
        setSubscribeDecision(SocketOptions.PUB_REJECT_SUBSCRIBE_BYTES, routingId);
    }

    Message welcomeMsg() {
        byte[] value = ContractAccess.socketGetOption(socket, SocketOptions.XPUB_WELCOME_MSG_BYTES);
        return Message.from(value);
    }

    public Message welcomeMessage() {
        return welcomeMsg();
    }

    void welcomeMsg(Message message) {
        Objects.requireNonNull(message, "message");
        ContractAccess.socketSetOption(socket, SocketOptions.XPUB_WELCOME_MSG_BYTES,
            message.toByteArray());
    }

    public void welcomeMessage(Message message) {
        welcomeMsg(message);
    }

    public int topicsCount() {
        return ContractAccess.socketGetOption(socket, SocketOptions.TOPICS_COUNT);
    }

    private void setSubscribeDecision(SocketOptionKey<byte[]> option,
                                      RoutingId routingId) {
        Objects.requireNonNull(routingId, "routingId");
        ContractAccess.socketSetOption(socket, option, routingId.toBytes());
    }
}
