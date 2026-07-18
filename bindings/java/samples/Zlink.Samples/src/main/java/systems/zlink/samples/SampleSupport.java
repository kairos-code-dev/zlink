/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.samples;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.eventing.MonitorEventType;
import systems.zlink.contracts.eventing.SocketMonitor;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.service.spot.Actor;
import systems.zlink.contracts.service.spot.ActorJoinDecision;
import systems.zlink.contracts.service.spot.ActorLocation;
import systems.zlink.contracts.service.spot.ActorRef;
import systems.zlink.contracts.service.spot.Claim;
import systems.zlink.contracts.service.spot.ClaimRecvResult;
import systems.zlink.contracts.service.spot.Dispatch;
import systems.zlink.contracts.service.spot.MeshNode;
import systems.zlink.contracts.service.spot.OperationId;
import systems.zlink.contracts.service.spot.OperationKind;
import systems.zlink.contracts.service.spot.ReadyBatch;
import systems.zlink.contracts.service.spot.ReadyDomain;
import systems.zlink.contracts.service.spot.ReceiveBatch;
import systems.zlink.contracts.service.spot.ReceiveRecord;
import systems.zlink.contracts.service.spot.RecordKind;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.service.spot.StreamSessionBinding;
import systems.zlink.contracts.service.spot.StreamSessionService;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.sockets.StreamSocket;
import systems.zlink.contracts.core.Zlink;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.Consumer;

final class SampleSupport {
    private static final Duration TIMEOUT = Duration.ofSeconds(5);
    static final String PAIR_PAYLOAD = "hello-pair";
    static final String DEALER_REQUEST = "ping";
    static final String DEALER_REPLY = "pong";
    static final String STREAM_PAYLOAD = "hello-stream";
    static final String PUBSUB_TOPIC = "prices";
    static final String PUBSUB_PAYLOAD = "101.25";
    private static final int TCP_PORT_BASE =
        40_000 + (int) (ProcessHandle.current().pid() % 10_000);
    private static final AtomicInteger NEXT_TCP_PORT =
        new AtomicInteger(TCP_PORT_BASE);

    private SampleSupport() {
    }

    /** Handles one received record during a {@link #pumpReady} drain. */
    @FunctionalInterface
    interface RecordHandler {
        void handle(ReceiveRecord record, ReceiveBatch batch, int index);
    }

    static String tcpEndpoint() {
        return "tcp://127.0.0.1:" + nextTcpPort();
    }

    private static int nextTcpPort() {
        for (int attempt = 0; attempt < 200; attempt++) {
            int port = NEXT_TCP_PORT.getAndIncrement();
            if (isBindable(port)) {
                return port;
            }
        }
        throw new IllegalStateException("failed to allocate tcp port");
    }

    private static boolean isBindable(int port) {
        try (ServerSocket server = new ServerSocket(port)) {
            server.setReuseAddress(false);
            return true;
        } catch (IOException ex) {
            return false;
        }
    }

    static void ensureNative() {
        Zlink.version();
    }

    static void await(CountDownLatch latch, String label) {
        try {
            if (!latch.await(TIMEOUT.toMillis(), TimeUnit.MILLISECONDS)) {
                throw new IllegalStateException(label + " timed out");
            }
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException(label + " interrupted", ex);
        }
    }

    static void waitUntil(String label, Condition condition) {
        long deadlineNanos = System.nanoTime() + TIMEOUT.toNanos();
        while (System.nanoTime() < deadlineNanos) {
            if (condition.check()) {
                return;
            }
            sleepQuietly(label);
        }
        throw new IllegalStateException(label + " timed out");
    }

    private static void sleepQuietly(String label) {
        try {
            TimeUnit.MILLISECONDS.sleep(10);
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException(label + " interrupted", ex);
        }
    }

    static String singleUtf8(Received received) {
        return received.singlePartOrThrow().toUtf8String();
    }

    static void waitConnected(SocketMonitor... monitors) {
        for (SocketMonitor monitor : monitors) {
            monitor.recv();
        }
    }

    static void waitStreamConnected(SocketMonitor monitor) {
        while (true) {
            var event = monitor.recv();
            if (event.event() == MonitorEventType.ACCEPTED
                || event.event() == MonitorEventType.CONNECTION_READY) {
                return;
            }
        }
    }

    static void waitPubSubReady(SocketMonitor pubMonitor,
                                SocketMonitor subMonitor) {
        waitMonitorEvent(subMonitor, MonitorEventType.CONNECTION_READY);
        waitMonitorEvent(pubMonitor, MonitorEventType.CONNECTION_READY);
    }

    /** Waits until the mesh node has admitted at least one peer. */
    static void waitMeshPeerConnected(MeshNode node) {
        waitUntil("mesh peer admission",
            () -> node.status().admittedPeerCount() > 0);
    }

    /**
     * Drains the node's ready index once and invokes {@code handler} for every
     * received record. The handler may take ownership of a record's parts with
     * {@code batch.retainMessage(index)} and answer request/control records via
     * {@link Dispatch}.
     */
    static void pumpReady(MeshNode node, ReadyBatch ready, ReceiveBatch recv,
                          RecordHandler handler) {
        pumpReady(node, ready, recv, handler, ReadyDomain.mask(ReadyDomain.ALL));
    }

    static void pumpReady(MeshNode node, ReadyBatch ready, ReceiveBatch recv,
                          RecordHandler handler, int domains) {
        ready.reset();
        node.drainReady(domains, ready, RecvFlags.DONT_WAIT);
        int readyCount = ready.count();
        for (int i = 0; i < readyCount; i++) {
            try (Claim claim = ready.takeClaim(i)) {
                while (claim.valid()) {
                    recv.reset();
                    ClaimRecvResult result = claim.recvBatch(recv,
                        RecvFlags.DONT_WAIT);
                    if (result.resultCode() != 0) {
                        break;
                    }
                    int count = recv.count();
                    for (int r = 0; r < count; r++) {
                        handler.handle(recv.at(r), recv, r);
                    }
                }
            }
        }
    }

    /**
     * Joins a local actor to a spot hosted on the same node: submits the join,
     * admits the host-side control record with {@code admitPayload}, waits for
     * the join completion, and returns the actor's membership epoch.
     */
    static long joinLocalSpot(MeshNode node, Actor actor, Spot spot,
                              String joinPayload, String admitPayload,
                              Consumer<String> onHostObservedJoin) {
        try (ReadyBatch ready = ReadyBatch.create(16);
             ReceiveBatch recv = ReceiveBatch.create(64, 256, 1 << 16)) {
            OperationId joinOp;
            try (Message join = Message.from(joinPayload)) {
                joinOp = actor.joinSpot(node.status().routingId(),
                    spot.routingId(), spot.status().lifecycleGeneration(),
                    List.of(join), TIMEOUT);
            }
            boolean[] completed = {false};
            int[] terminal = {-1};
            long deadline = System.nanoTime() + TIMEOUT.toNanos();
            while (!completed[0] && System.nanoTime() < deadline) {
                pumpReady(node, ready, recv, (record, batch, index) -> {
                    if (record.kind() == RecordKind.SPOT_CONTROL
                        && record.operationKind() == OperationKind.ACTOR_JOIN) {
                        List<Message> request = batch.retainMessage(index);
                        if (onHostObservedJoin != null && !request.isEmpty()) {
                            onHostObservedJoin.accept(
                                request.get(0).toUtf8String());
                        }
                        try (Message ok = Message.from(admitPayload)) {
                            Dispatch.actorJoinReply(record.replyToken(),
                                ActorJoinDecision.ACCEPTED, List.of(ok),
                                SendFlags.NONE);
                        }
                        closeAll(request);
                    } else if (record.kind() == RecordKind.COMPLETION
                        && record.operationKind() == OperationKind.ACTOR_JOIN
                        && record.operationId().equals(joinOp)) {
                        terminal[0] = record.terminalResult();
                        completed[0] = true;
                    }
                });
                if (completed[0]) {
                    break;
                }
                sleepQuietly("actor join");
            }
            if (!completed[0]) {
                throw new IllegalStateException("actor join did not complete");
            }
            if (terminal[0] != 0) {
                throw new IllegalStateException(
                    "actor join rejected: " + terminal[0]);
            }
            return node.actorLookup(actor.ref().actorId())
                .map(ActorLocation::membershipEpoch)
                .orElse(0L);
        }
    }

    /** Leaves a spot the actor previously joined (best effort completion wait). */
    static void leaveLocalSpot(MeshNode node, Actor actor, long membershipEpoch) {
        try (ReadyBatch ready = ReadyBatch.create(16);
             ReceiveBatch recv = ReceiveBatch.create(64, 256, 1 << 16)) {
            OperationId leaveOp = actor.leaveSpot(membershipEpoch, TIMEOUT);
            boolean[] completed = {false};
            long deadline = System.nanoTime() + TIMEOUT.toNanos();
            while (!completed[0] && System.nanoTime() < deadline) {
                pumpReady(node, ready, recv, (record, batch, index) -> {
                    if (record.kind() == RecordKind.COMPLETION
                        && record.operationKind() == OperationKind.ACTOR_LEAVE
                        && record.operationId().equals(leaveOp)) {
                        completed[0] = true;
                    }
                });
                if (completed[0]) {
                    break;
                }
                sleepQuietly("actor leave");
            }
        }
    }

    /** Creates and starts a STREAM session service for a stream socket. */
    static StreamSessionService startSessionService(MeshNode node,
                                                    StreamSocket stream) {
        StreamSessionService service = StreamSessionService.create(node, stream);
        service.start();
        return service;
    }

    /** Binds an actor to a STREAM session and waits for the bind completion. */
    static void bindSessionActor(MeshNode node, StreamSessionService service,
                                 RoutingId sessionRid, ActorRef actor) {
        OperationId op = service.bindActor(sessionRid, actor, TIMEOUT);
        waitStreamOperation(node, op, "bind");
    }

    /** Unbinds an actor from a STREAM session through the session service. */
    static void unbindSessionActor(MeshNode node, StreamSessionService service,
                                   RoutingId sessionRid, ActorRef actor) {
        long generation = 0;
        for (StreamSessionBinding binding : service.bindings(sessionRid)) {
            if (binding.actor().actorId().equals(actor.actorId())) {
                generation = binding.bindingGeneration();
            }
        }
        OperationId op = service.unbindActor(sessionRid, actor, generation,
            TIMEOUT);
        waitStreamOperation(node, op, "unbind");
    }

    /** Relays a payload to a session-bound actor via the session service. */
    static void relaySessionMessage(StreamSessionService service,
                                    RoutingId sessionRid, ActorRef actor,
                                    String payload) {
        try (Message message = Message.from(payload)) {
            service.sendToActor(sessionRid, actor, List.of(message),
                SendFlags.NONE);
        }
    }

    private static void waitStreamOperation(MeshNode node, OperationId opId,
                                            String name) {
        try (ReadyBatch ready = ReadyBatch.create(16);
             ReceiveBatch recv = ReceiveBatch.create(64, 256, 1 << 16)) {
            boolean[] completed = {false};
            int[] terminal = {0};
            long deadline = System.nanoTime() + TIMEOUT.toNanos();
            while (!completed[0] && System.nanoTime() < deadline) {
                pumpReady(node, ready, recv, (record, batch, index) -> {
                    if (record.kind() == RecordKind.COMPLETION
                        && record.operationId().equals(opId)) {
                        terminal[0] = record.terminalResult();
                        completed[0] = true;
                    }
                });
                if (completed[0]) {
                    break;
                }
                sleepQuietly("stream session " + name);
            }
            if (!completed[0]) {
                throw new IllegalStateException(
                    "stream session " + name + " did not complete");
            }
            if (terminal[0] != 0) {
                throw new IllegalStateException(
                    "stream session " + name + " failed: " + terminal[0]);
            }
        }
    }

    /**
     * Drains the node once and appends every actor-addressed message payload
     * into {@code sink} in arrival order.
     */
    static void collectActorMessages(MeshNode node, ReadyBatch ready,
                                     ReceiveBatch recv, List<String> sink) {
        pumpReady(node, ready, recv, (record, batch, index) -> {
            if (record.kind() != RecordKind.ACTOR_SEND
                || record.partCount() == 0) {
                return;
            }
            List<Message> parts = batch.retainMessage(index);
            if (!parts.isEmpty()) {
                sink.add(parts.get(0).toUtf8String());
            }
            closeAll(parts);
        });
    }

    static void closeAll(List<Message> parts) {
        for (Message part : parts) {
            closeQuietly(part);
        }
    }

    static void closeQuietly(AutoCloseable resource) {
        if (resource == null) {
            return;
        }
        try {
            resource.close();
        } catch (Exception ignored) {
        }
    }

    static java.net.Socket connectRawTcp(String endpoint) {
        try {
            InetSocketAddress address = tcpAddress(endpoint);
            java.net.Socket socket = new java.net.Socket();
            socket.connect(address, (int) TIMEOUT.toMillis());
            socket.setSoTimeout((int) TIMEOUT.toMillis());
            return socket;
        } catch (IOException ex) {
            throw new IllegalStateException("failed to connect raw tcp client", ex);
        }
    }

    static void sendRawTcp(java.net.Socket socket, byte[] payload) {
        try {
            OutputStream out = socket.getOutputStream();
            out.write(payload);
            out.flush();
        } catch (IOException ex) {
            throw new IllegalStateException("failed to send raw tcp payload", ex);
        }
    }

    /** Sends a STREAM socket packet frame (empty header, big-endian lengths). */
    static void sendStreamPacket(java.net.Socket socket, byte[] body) {
        byte[] frame = new byte[6 + body.length];
        frame[2] = (byte) (body.length >>> 24);
        frame[3] = (byte) (body.length >>> 16);
        frame[4] = (byte) (body.length >>> 8);
        frame[5] = (byte) body.length;
        System.arraycopy(body, 0, frame, 6, body.length);
        sendRawTcp(socket, frame);
    }

    static byte[] recvExactRawTcp(java.net.Socket socket, int length) {
        try {
            InputStream in = socket.getInputStream();
            return readExactly(in, length);
        } catch (IOException ex) {
            throw new IllegalStateException("failed to receive raw tcp payload", ex);
        }
    }

    private static InetSocketAddress tcpAddress(String endpoint) {
        if (!endpoint.startsWith("tcp://")) {
            throw new IllegalArgumentException(
                "raw tcp helper requires tcp:// endpoint: " + endpoint);
        }
        String hostPort = endpoint.substring("tcp://".length());
        int colon = hostPort.lastIndexOf(':');
        if (colon <= 0 || colon == hostPort.length() - 1) {
            throw new IllegalArgumentException("invalid tcp endpoint: " + endpoint);
        }
        String host = hostPort.substring(0, colon);
        int port = Integer.parseInt(hostPort.substring(colon + 1));
        return new InetSocketAddress(host, port);
    }

    private static byte[] readExactly(InputStream in, int length)
      throws IOException {
        byte[] data = new byte[length];
        int read = 0;
        while (read < length) {
            int n = in.read(data, read, length - read);
            if (n < 0) {
                throw new IOException("unexpected end of stream");
            }
            read += n;
        }
        return data;
    }

    private static void waitMonitorEvent(SocketMonitor monitor,
                                         MonitorEventType eventType) {
        while (true) {
            var event = monitor.recv();
            if (event.event() == eventType) {
                return;
            }
        }
    }

    @FunctionalInterface
    interface Condition {
        boolean check();
    }
}
