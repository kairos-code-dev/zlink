/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink;

import systems.zlink.SocketOptionKey;
import systems.zlink.SocketOptions;
import java.util.Objects;

public class PubSocketOptions extends CommonSocketOptions {
    private boolean manualEnabled;

    PubSocketOptions(Socket socket) {
        super(socket);
    }

    public boolean verbose() {
        return socket.getOption(SocketOptions.XPUB_VERBOSE) != 0;
    }

    public void verbose(boolean enabled) {
        socket.setOption(SocketOptions.XPUB_VERBOSE, enabled ? 1 : 0);
    }

    public boolean verboser() {
        return socket.getOption(SocketOptions.XPUB_VERBOSER) != 0;
    }

    public void verboser(boolean enabled) {
        socket.setOption(SocketOptions.XPUB_VERBOSER, enabled ? 1 : 0);
    }

    public boolean noDrop() {
        return socket.getOption(SocketOptions.XPUB_NODROP) != 0;
    }

    public void noDrop(boolean enabled) {
        socket.setOption(SocketOptions.XPUB_NODROP, enabled ? 1 : 0);
    }

    public boolean manual() {
        return manualEnabled;
    }

    public void manual(boolean enabled) {
        manualEnabled = enabled;
        socket.setOption(SocketOptions.XPUB_MANUAL, enabled ? 1 : 0);
    }

    public boolean manualLastValue() {
        return socket.getOption(SocketOptions.XPUB_MANUAL_LAST_VALUE) != 0;
    }

    public void manualLastValue(boolean enabled) {
        socket.setOption(SocketOptions.XPUB_MANUAL_LAST_VALUE, enabled ? 1 : 0);
    }

    public void approveSubscribe(RoutingId routingId) {
        setSubscribeDecision(SocketOptions.PUB_APPROVE_SUBSCRIBE_BYTES, routingId);
    }

    public void rejectSubscribe(RoutingId routingId) {
        setSubscribeDecision(SocketOptions.PUB_REJECT_SUBSCRIBE_BYTES, routingId);
    }

    Message welcomeMsg() {
        byte[] value = socket.getOption(SocketOptions.XPUB_WELCOME_MSG_BYTES);
        return Message.copyOf(value);
    }

    public Message welcomeMessage() {
        return welcomeMsg();
    }

    void welcomeMsg(Message message) {
        Objects.requireNonNull(message, "message");
        socket.setSockOptRaw(SocketOptions.XPUB_WELCOME_MSG.optionId(),
            message.dataSegment(), message.size());
    }

    public void welcomeMessage(Message message) {
        welcomeMsg(message);
    }

    public int topicsCount() {
        return socket.getOption(SocketOptions.TOPICS_COUNT);
    }

    private void setSubscribeDecision(SocketOptionKey<byte[]> option,
                                      RoutingId routingId) {
        Objects.requireNonNull(routingId, "routingId");
        socket.setOption(option, routingId.toBytes());
    }
}
