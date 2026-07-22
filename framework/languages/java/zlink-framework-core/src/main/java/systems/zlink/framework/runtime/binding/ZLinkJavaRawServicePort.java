package systems.zlink.framework.runtime.binding;

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.concurrent.atomic.AtomicBoolean;
import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.messaging.SendSubmitOperation;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.contracts.sockets.RouterSocket;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.framework.runtime.service.ZLinkServiceWireCodec;
import systems.zlink.framework.runtime.service.ZLinkServiceWireFrame;

/**
 * Private binding-facing port for the JVM service runtime.
 *
 * <p>This is the only service runtime class that constructs binding Context,
 * RouterSocket, Received, and Message objects. It copies received frames before
 * returning so binding-owned storage never reaches a handler or mailbox.
 */
final class ZLinkJavaRawServicePort implements AutoCloseable {
    private final Context context;
    private final boolean ownsContext;
    private final List<RouterSocket> routers = new ArrayList<>();
    private final AtomicBoolean closed = new AtomicBoolean();

    ZLinkJavaRawServicePort() {
        this(Zlink.createContext(), true);
    }

    ZLinkJavaRawServicePort(Context context) {
        this(context, false);
    }

    private ZLinkJavaRawServicePort(Context context, boolean ownsContext) {
        this.context = Objects.requireNonNull(context, "context");
        this.ownsContext = ownsContext;
    }

    synchronized RouterSocket openRouter(RoutingId routingId) {
        ensureOpen();
        RouterSocket router = context.createRouterSocket();
        boolean accepted = false;
        try {
            router.setRoutingId(Objects.requireNonNull(routingId, "routingId"));
            routers.add(router);
            accepted = true;
            return router;
        } finally {
            if (!accepted) {
                router.close();
            }
        }
    }

    boolean send(RouterSocket router, RoutingId target, List<byte[]> frames) {
        ensureOwned(router);
        Objects.requireNonNull(target, "target");
        if (frames.isEmpty()) {
            throw new IllegalArgumentException("service multipart must not be empty");
        }
        List<Message> messages = frames.stream()
            .map(frame -> Message.from(Objects.requireNonNull(frame, "frame")))
            .toList();
        boolean submitted = false;
        try {
            SendSubmitOperation submit = router.send(target).message(messages.getFirst());
            for (int index = 1; index < messages.size(); index++) {
                submit.message(messages.get(index));
            }
            submitted = submit.flags(SendFlags.DONT_WAIT).submit();
            return submitted;
        } finally {
            if (!submitted) {
                messages.forEach(Message::close);
            }
        }
    }

    boolean sendService(
        RouterSocket router,
        RoutingId target,
        int command,
        int flags,
        List<byte[]> frames) {
        return send(router, target, new ZLinkServiceWireCodec().encode(
            new ZLinkServiceWireFrame(command, flags, frames)));
    }

    Optional<Inbound> receive(RouterSocket router) {
        ensureOwned(router);
        try (Received received = new Received()) {
            if (!router.recv(received, RecvFlags.DONT_WAIT)) {
                return Optional.empty();
            }
            RoutingId source = received.getRoutingId().orElseThrow(
                () -> new IllegalStateException("service ROUTER receive has no routing id"));
            List<byte[]> frames = received.parts().stream().map(Message::toByteArray).toList();
            return Optional.of(new Inbound(source, frames));
        }
    }

    @Override
    public synchronized void close() {
        if (!closed.compareAndSet(false, true)) {
            return;
        }
        for (int index = routers.size() - 1; index >= 0; index--) {
            routers.get(index).close();
        }
        routers.clear();
        if (ownsContext) {
            context.close();
        }
    }

    private synchronized void ensureOwned(RouterSocket router) {
        ensureOpen();
        if (!routers.contains(Objects.requireNonNull(router, "router"))) {
            throw new IllegalArgumentException("router is not owned by this service port");
        }
    }

    private void ensureOpen() {
        if (closed.get()) {
            throw new IllegalStateException("service port is closed");
        }
    }

    record Inbound(RoutingId source, List<byte[]> frames) {
        Inbound {
            Objects.requireNonNull(source, "source");
            frames = frames.stream().map(byte[]::clone).toList();
        }

        @Override
        public List<byte[]> frames() {
            return frames.stream().map(byte[]::clone).toList();
        }
    }
}
