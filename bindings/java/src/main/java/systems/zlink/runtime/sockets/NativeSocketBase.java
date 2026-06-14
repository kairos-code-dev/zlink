/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.sockets;

import systems.zlink.contracts.internal.sockets.SocketOptionKey;

import systems.zlink.contracts.sockets.*;

import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.eventing.MonitorEventType;
import systems.zlink.contracts.eventing.SocketMonitor;
import systems.zlink.contracts.internal.ContractAccess;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.messaging.SubscriptionEntry;
import systems.zlink.contracts.messaging.SubscriptionEvent;
import systems.zlink.contracts.messaging.TopicMessage;
import systems.zlink.runtime.nativeapi.InternalAccess;
import io.netty.buffer.ByteBuf;
import java.lang.foreign.MemorySegment;
import java.util.List;
import java.util.Objects;
import java.util.Optional;

/**
 * Common contract facade for zlink typed socket resources.
 */
abstract class NativeSocketBase implements Socket {
    private final CommonSocketOptions options;
    private final NativeSocketRuntime runtime;

    static {
        InternalAccess.register(new InternalAccess.SocketAccess() {
            @Override
            public MemorySegment handle(Socket socket) {
                return nativeSocket(socket).nativeHandle();
            }

            @Override
            public void setOption(Socket socket, SocketOptionKey<Integer> option,
                                  int value) {
                nativeSocket(socket).setOption(option, value);
            }

            @Override
            public void setOption(Socket socket, SocketOptionKey<Long> option,
                                  long value) {
                nativeSocket(socket).setOption(option, value);
            }

            @Override
            public void setOption(Socket socket, SocketOptionKey<String> option,
                                  String value) {
                nativeSocket(socket).setOption(option, value);
            }

            @Override
            public void setOption(Socket socket, SocketOptionKey<byte[]> option,
                                  byte[] value) {
                nativeSocket(socket).setOption(option, value);
            }

            @Override
            public <T> T getOption(Socket socket, SocketOptionKey<T> option) {
                return nativeSocket(socket).getOption(option);
            }

            @Override
            public void setDealerIntOption(Socket socket, int option,
                                           int value) {
                nativeSocket(socket).setDealerIntOption(option, value);
            }

            @Override
            public int getDealerIntOption(Socket socket, int option) {
                return nativeSocket(socket).getDealerIntOption(option);
            }

            @Override
            public int getRouterIntOption(Socket socket, int option) {
                return nativeSocket(socket).getRouterIntOption(option);
            }

            @Override
            public void setRouterIntOption(Socket socket, int option,
                                           int value) {
                nativeSocket(socket).setRouterIntOption(option, value);
            }

            @Override
            public boolean inCallback() {
                return NativeSocketRuntime.inCallbackContext();
            }

            @Override
            public void enterCallback() {
                NativeSocketRuntime.enterCallbackContext();
            }

            @Override
            public void leaveCallback() {
                NativeSocketRuntime.leaveCallbackContext();
            }

            private NativeSocketBase nativeSocket(Socket socket) {
                if (socket instanceof NativeSocketBase nativeSocket)
                    return nativeSocket;
                throw new IllegalArgumentException(
                    "socket is not backed by the native zlink runtime");
            }
        });
        ContractAccess.register(new ContractAccess.SocketOptionsAccess() {
            @Override
            public void setOption(Socket socket, SocketOptionKey<Integer> option,
                                  int value) {
                nativeSocket(socket).setOption(option, value);
            }

            @Override
            public void setOption(Socket socket, SocketOptionKey<Long> option,
                                  long value) {
                nativeSocket(socket).setOption(option, value);
            }

            @Override
            public void setOption(Socket socket, SocketOptionKey<String> option,
                                  String value) {
                nativeSocket(socket).setOption(option, value);
            }

            @Override
            public void setOption(Socket socket, SocketOptionKey<byte[]> option,
                                  byte[] value) {
                nativeSocket(socket).setOption(option, value);
            }

            @Override
            public <T> T getOption(Socket socket, SocketOptionKey<T> option) {
                return nativeSocket(socket).getOption(option);
            }

            @Override
            public void setDealerIntOption(Socket socket, int option,
                                           int value) {
                nativeSocket(socket).setDealerIntOption(option, value);
            }

            @Override
            public int getDealerIntOption(Socket socket, int option) {
                return nativeSocket(socket).getDealerIntOption(option);
            }

            @Override
            public int getRouterIntOption(Socket socket, int option) {
                return nativeSocket(socket).getRouterIntOption(option);
            }

            @Override
            public void setRouterIntOption(Socket socket, int option,
                                           int value) {
                nativeSocket(socket).setRouterIntOption(option, value);
            }

            private NativeSocketBase nativeSocket(Socket socket) {
                if (socket instanceof NativeSocketBase nativeSocket)
                    return nativeSocket;
                throw new IllegalArgumentException(
                    "socket is not backed by the native zlink runtime");
            }
        });
    }

    protected NativeSocketBase(Context ctx, SocketType type) {
        Objects.requireNonNull(ctx, "ctx");
        Objects.requireNonNull(type, "type");
        this.options = ContractAccess.commonSocketOptions(this);
        this.runtime = new NativeSocketRuntime(ctx, type);
    }

    protected NativeSocketBase(MemorySegment handle, boolean own,
                     SocketType socketTypeHint) {
        Objects.requireNonNull(handle, "handle");
        this.options = ContractAccess.commonSocketOptions(this);
        this.runtime = new NativeSocketRuntime(handle, own, socketTypeHint);
    }

    public void bind(String endpoint) { runtime.bind(endpoint); }
    public void connect(String endpoint) { runtime.connect(endpoint); }
    public void unbind(String endpoint) { runtime.unbind(endpoint); }
    public void disconnect(String endpoint) { runtime.disconnect(endpoint); }
    public void disconnectRid(RoutingId peerRid) { runtime.disconnectRid(peerRid); }
    public void attachDiscovery(systems.zlink.contracts.service.discovery.Discovery discovery) { runtime.attachDiscovery(discovery); }
    public void setChannelName(String channelName) { runtime.setChannelName(channelName); }
    public String getChannelName() { return runtime.getChannelName(); }
    void attachStreamRaw(StreamRawPacketHandler handler) { runtime.attachStreamRaw(handler); }
    void attachStreamRaw(StreamUInt32RawNativeHandler handler) { runtime.attachStreamRaw(handler); }
    void attachStreamPacket(StreamFramedPacketHandler handler) { runtime.attachStreamPacket(handler); }
    void attachStreamPacket(StreamUInt32FramedPacketHandler handler) { runtime.attachStreamPacket(handler); }
    void attachStreamPacket(StreamUInt32FramedNativeHandler handler) { runtime.attachStreamPacket(handler); }
    void detachStream() { runtime.detachStream(); }
    void setOption(SocketOptionKey<Integer> option, int value) { runtime.setOption(option, value); }
    void setOption(SocketOptionKey<Long> option, long value) { runtime.setOption(option, value); }
    void setOption(SocketOptionKey<String> option, String value) { runtime.setOption(option, value); }
    void setOption(SocketOptionKey<byte[]> option, byte[] value) { runtime.setOption(option, value); }
    <T> T getOption(SocketOptionKey<T> option) { return runtime.getOption(option); }

    public CommonSocketOptions options() { return options; }
    public SocketMonitor monitorOpen() { return runtime.monitorOpen(); }
    public SocketMonitor monitorOpen(MonitorEventType... events) { return runtime.monitorOpen(events); }
    public final void setTlsServer(String certPem, String keyPem,
                                   boolean requireClientCert) { runtime.setTlsServer(certPem, keyPem, requireClientCert); }
    public final void setTlsClient(String caCertPem, String hostname,
                                   boolean trustSystem) { runtime.setTlsClient(caCertPem, hostname, trustSystem); }
    boolean send(Message part) { return runtime.send(part); }
    boolean send(Message part, SendFlag flags) { return runtime.send(part, flags); }
    void sendMessageFrame(RoutingId routingId, Message message, SendFlag flag) { runtime.sendMessageFrame(routingId, message, flag); }
    boolean send(List<Message> parts) { return runtime.send(parts); }
    boolean send(List<Message> parts, SendFlag flags) { return runtime.send(parts, flags); }
    SendResult sendNoWaitResult(Message part) { return runtime.sendNoWaitResult(part); }
    SendResult sendMessageFrameNoWaitResult(RoutingId routingId, Message message) { return runtime.sendMessageFrameNoWaitResult(routingId, message); }
    SendResult sendNoWaitResult(List<Message> parts) { return runtime.sendNoWaitResult(parts); }
    boolean send(RoutingId rid, Message part) { return runtime.send(rid, part); }
    boolean send(RoutingId rid, Message part, SendFlag flags) { return runtime.send(rid, part, flags); }
    boolean send(byte[] routingIdBytes, Message part, SendFlag flags) { return runtime.send(routingIdBytes, part, flags); }
    void send(int rid, Message part, SendFlag flags) { runtime.send(rid, part, flags); }
    int send(int rid, MemorySegment payload, int length, int sendFlags) { return runtime.send(rid, payload, length, sendFlags); }
    int sendCopied(int rid, MemorySegment payload, int length, int sendFlags) { return runtime.sendCopied(rid, payload, length, sendFlags); }
    boolean send(RoutingId rid, List<Message> parts) { return runtime.send(rid, parts); }
    boolean send(RoutingId rid, List<Message> parts, SendFlag flags) { return runtime.send(rid, parts, flags); }
    SendResult sendNoWaitResult(RoutingId rid, Message part) { return runtime.sendNoWaitResult(rid, part); }
    SendResult sendNoWaitResult(RoutingId rid, List<Message> parts) { return runtime.sendNoWaitResult(rid, parts); }
    boolean publish(String topicId, Message part) { return runtime.publish(topicId, part); }
    boolean publish(String topicId, Message part, SendFlag flags) { return runtime.publish(topicId, part, flags); }
    boolean publish(String topicId, List<Message> parts) { return runtime.publish(topicId, parts); }
    boolean publish(String topicId, List<Message> parts, SendFlag flags) { return runtime.publish(topicId, parts, flags); }
    SendResult publishNoWaitResult(String topicId, Message part) { return runtime.publishNoWaitResult(topicId, part); }
    SendResult publishNoWaitResult(String topicId, List<Message> parts) { return runtime.publishNoWaitResult(topicId, parts); }
    Received recv() { return runtime.recv(); }
    Received recv(ReceiveFlag flags) { return runtime.recv(flags); }
    Received recvNoWaitOrNull() { return runtime.recvNoWaitOrNull(); }
    boolean recvInto(Received result, ReceiveFlag flags) { return runtime.recvInto(result, flags); }
    Optional<Received> recvNoWait() { return runtime.recvNoWait(); }
    Received recvLazy(ReceiveFlag flags) { return runtime.recvLazy(flags); }
    Received recvLazyNoWaitOrNull() { return runtime.recvLazyNoWaitOrNull(); }
    boolean subscribe(TopicMessage result, ReceiveFlag flags) { return runtime.subscribe(result, flags); }
    Optional<TopicMessage> subscribeNoWait() { return runtime.subscribeNoWait(); }
    boolean receiveSubscriptionEvent(SubscriptionEvent result, ReceiveFlag flags) { return runtime.receiveSubscriptionEvent(result, flags); }
    Optional<SubscriptionEvent> tryReceiveSubscriptionEvent() { return runtime.tryReceiveSubscriptionEvent(); }
    public void setRoutingId(RoutingId rid) { runtime.setRoutingId(rid); }
    public RoutingId getRoutingId() { return runtime.getRoutingId(); }
    public void setSubscription(String filter) { runtime.setSubscription(filter); }
    void setSubscription(byte[] filter) { runtime.setSubscription(filter); }
    public void unsetSubscription(String filter) { runtime.unsetSubscription(filter); }
    void unsetSubscription(byte[] filter) { runtime.unsetSubscription(filter); }
    List<SubscriptionEntry> subscriptions() { return runtime.subscriptions(); }
    void onReceive(SocketMessageHandler handler) { runtime.onReceive(handler); }
    void onSubscribe(SubscribeHandler handler) { runtime.onSubscribe(handler); }
    public void setSendReadyHandler(SendReadyHandler handler) { runtime.setSendReadyHandler(handler); }
    int send(byte[] data, int offset, int length, int sendFlags) { return runtime.send(data, offset, length, sendFlags); }
    boolean sendNoWaitResult(byte[] data, int offset, int length, int sendFlags) { return runtime.sendNoWaitResult(data, offset, length, sendFlags); }
    int send(MemorySegment segment, long offset, long length, int sendFlags) { return runtime.send(segment, offset, length, sendFlags); }
    boolean sendNoWaitResult(MemorySegment segment, long offset, long length, int sendFlags) { return runtime.sendNoWaitResult(segment, offset, length, sendFlags); }
    int send(ByteBuf buf, int sendFlags) { return runtime.send(buf, sendFlags); }
    boolean sendNoWaitResult(ByteBuf buf, int sendFlags) { return runtime.sendNoWaitResult(buf, sendFlags); }
    int recv(MemorySegment segment, long offset, long length, ReceiveFlag flags) { return runtime.recv(segment, offset, length, flags); }
    int recvNoWait(MemorySegment segment, long offset, long length, ReceiveFlag flags) { return runtime.recvNoWait(segment, offset, length, flags); }
    @Override public void close() { runtime.close(); }
    void closeInternal() { runtime.closeInternal(); }
    <T> T readOption(SocketOptionKey<T> option) { return runtime.readOption(option); }
    int recvByteBufDirect(ByteBuf buf, ReceiveFlag flags) { return runtime.recvByteBufDirect(buf, flags); }
    int recvByteBufDirectNoWait(ByteBuf buf, ReceiveFlag flags) { return runtime.recvByteBufDirectNoWait(buf, flags); }
    void validateOptionAccess(int optionId, String optionName) { runtime.validateOptionAccess(optionId, optionName); }
    void setSockOptRaw(int optionId, MemorySegment value, long len) { runtime.setSockOptRaw(optionId, value, len); }
    void setSockOptBytes(int optionId, byte[] value, int offset, int length) { runtime.setSockOptBytes(optionId, value, offset, length); }
    void setTypedBytesOption(int optionId, byte[] value, int offset, int length) { runtime.setTypedBytesOption(optionId, value, offset, length); }
    void setRoutingIdBytes(byte[] value, int offset, int length) { runtime.setRoutingIdBytes(value, offset, length); }
    byte[] getRoutingIdBytes() { return runtime.getRoutingIdBytes(); }
    void setSubscriptionBytes(byte[] value, int offset, int length, boolean subscribe) { runtime.setSubscriptionBytes(value, offset, length, subscribe); }
    void setSockOptInt(int optionId, int value) { runtime.setSockOptInt(optionId, value); }
    void setSockOptLong(int optionId, long value) { runtime.setSockOptLong(optionId, value); }
    byte[] getSockOptBytes(int optionId, int maxLen) { return runtime.getSockOptBytes(optionId, maxLen); }
    int getSockOptInt(int optionId) { return runtime.getSockOptInt(optionId); }
    void sendMessageFrame(Message message, SendFlag flag) { runtime.sendMessageFrame(message, flag); }
    boolean sendMessageFrameNoWaitResult(Message message, SendFlag flag) { return runtime.sendMessageFrameNoWaitResult(message, flag); }
    SendResult sendMessageFrameNoWaitResult(Message message) { return runtime.sendMessageFrameNoWaitResult(message); }
    void recvMessageFrame(Message message, ReceiveFlag flag) { runtime.recvMessageFrame(message, flag); }
    int recvMessageFrameNoWait(Message message, ReceiveFlag flag) { return runtime.recvMessageFrameNoWait(message, flag); }
    void sendParts(RoutingId routingId, List<Message> parts, SendFlag flags, boolean cloneParts) { runtime.sendParts(routingId, parts, flags, cloneParts); }
    SendResult sendNoWaitPartsResult(RoutingId routingId, List<Message> parts) { return runtime.sendNoWaitPartsResult(routingId, parts); }
    void publishParts(String topicId, List<Message> parts, SendFlag flags, boolean cloneParts) { runtime.publishParts(topicId, parts, flags, cloneParts); }
    SendResult publishNoWaitPartsResult(String topicId, List<Message> parts) { return runtime.publishNoWaitPartsResult(topicId, parts); }
    void prepareRecvLikeOperation() { runtime.prepareRecvLikeOperation(); }
    Received registerLazyReceive(Received received, boolean hasMore) { return runtime.registerLazyReceive(received, hasMore); }
    byte[] subscriptionAt(long index, MemorySegment lenInOut, MemorySegment topicOut) { return runtime.subscriptionAt(index, lenInOut, topicOut, NativeSocketRuntime.TOPIC_CAPACITY); }
    void ensureOpen() { runtime.ensureOpen(); }
    void setDealerIntOption(int option, int value) { runtime.setDealerIntOption(option, value); }
    int getDealerIntOption(int option) { return runtime.getDealerIntOption(option); }
    int getRouterIntOption(int option) { return runtime.getRouterIntOption(option); }
    void setRouterIntOption(int option, int value) { runtime.setRouterIntOption(option, value); }

    MemorySegment nativeHandle() { return runtime.handle(); }
    MemorySegment handle() { return runtime.handle(); }
}
