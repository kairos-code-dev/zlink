package systems.zlink.framework.runtime.binding;

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.concurrent.atomic.AtomicBoolean;
import java.time.Duration;
import java.util.function.BiConsumer;
import java.util.function.Predicate;
import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.messaging.SendSubmitOperation;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.contracts.sockets.RouterSocket;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.sockets.RequestResult;
import systems.zlink.contracts.eventing.MonitorEventType;
import systems.zlink.contracts.eventing.PollEventFlags;
import systems.zlink.contracts.eventing.PollEvents;
import systems.zlink.contracts.eventing.Poller;
import systems.zlink.contracts.eventing.SocketMonitor;
import systems.zlink.framework.runtime.internal.service.ZLinkServiceWireCodec;
import systems.zlink.framework.runtime.internal.service.ZLinkServiceWireFrame;

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
    private final Predicate<List<byte[]>> completionControlSelector;
    private final List<RouterSocket> routers = new ArrayList<>();
    private final AtomicBoolean closed = new AtomicBoolean();
    private Poller completionPoller;

    ZLinkJavaRawServicePort() {
        this(Zlink.createContext(), true, ignored -> false);
    }

    ZLinkJavaRawServicePort(Context context) {
        this(context, false, ignored -> false);
    }

    ZLinkJavaRawServicePort(
        Context context,
        Predicate<List<byte[]>> completionControlSelector) {
        this(context, false, completionControlSelector);
    }

    private ZLinkJavaRawServicePort(
        Context context,
        boolean ownsContext,
        Predicate<List<byte[]>> completionControlSelector) {
        this.context = Objects.requireNonNull(context, "context");
        this.ownsContext = ownsContext;
        this.completionControlSelector = Objects.requireNonNull(
            completionControlSelector, "completionControlSelector");
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

    synchronized void startCompletionControl(RouterSocket router) {
        ensureOwned(router);
        if (completionPoller != null) {
            throw new IllegalStateException(
                "completion control pump is already started");
        }
        Poller poller = Zlink.createPoller();
        boolean accepted = false;
        try {
            poller.add(router, 1L, PollEventFlags.POLLCOMPLETION);
            completionPoller = poller;
            accepted = true;
        } finally {
            if (!accepted) {
                poller.close();
                completionPoller = null;
            }
        }
    }

    void pollCompletionControl() {
        Poller poller;
        synchronized (this) {
            ensureOpen();
            poller = completionPoller;
        }
        if (poller != null) {
            poller.wait(new PollEvents(1), Duration.ZERO);
        }
    }

    boolean send(RouterSocket router, RoutingId target, List<byte[]> frames) {
        ensureOwned(router);
        Objects.requireNonNull(target, "target");
        if (frames.isEmpty()) {
            throw new IllegalArgumentException("service multipart must not be empty");
        }
        if (completionControlSelector.test(frames)) {
            return sendCompletionControl(router, target, frames);
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

    boolean sendCompletionControl(
        RouterSocket router,
        RoutingId target,
        List<byte[]> frames) {
        ensureOwned(router);
        Objects.requireNonNull(target, "target");
        if (frames.isEmpty()) {
            throw new IllegalArgumentException(
                "completion control multipart must not be empty");
        }
        List<Message> messages = frames.stream()
            .map(frame -> Message.from(Objects.requireNonNull(frame, "frame")))
            .toList();
        try {
            return router.trySendCompletionControl(target, messages);
        } finally {
            messages.forEach(Message::close);
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

    boolean request(
        RouterSocket router,
        RoutingId target,
        List<byte[]> frames,
        Duration timeout,
        BiConsumer<RequestResult, List<byte[]>> completion) {
        ensureOwned(router);
        Objects.requireNonNull(target, "target");
        Objects.requireNonNull(timeout, "timeout");
        Objects.requireNonNull(completion, "completion");
        if (frames.isEmpty()) {
            throw new IllegalArgumentException("service request must not be empty");
        }
        List<Message> messages = frames.stream()
            .map(frame -> Message.from(Objects.requireNonNull(frame, "frame")))
            .toList();
        boolean submitted = false;
        try {
            var submit = router.request(target).message(messages.getFirst());
            for (int index = 1; index < messages.size(); index++) {
                submit.message(messages.get(index));
            }
            submitted = submit.timeout(timeout)
                .flags(SendFlags.DONT_WAIT)
                .submit((result, reply) -> {
                    try {
                        completion.accept(
                            result,
                            reply.stream().map(Message::toByteArray).toList());
                    } finally {
                        reply.forEach(Message::close);
                    }
                });
            return submitted;
        } finally {
            if (!submitted) {
                messages.forEach(Message::close);
            }
        }
    }

    void reply(
        RouterSocket router,
        RoutingId target,
        long requestSequence,
        List<byte[]> frames) {
        ensureOwned(router);
        if (requestSequence <= 0 || frames.isEmpty()) {
            throw new IllegalArgumentException(
                "service reply requires request sequence and frames");
        }
        List<Message> messages = frames.stream()
            .map(frame -> Message.from(Objects.requireNonNull(frame, "frame")))
            .toList();
        boolean submitted = false;
        try {
            var submit = router.reply(target, requestSequence)
                .message(messages.getFirst());
            for (int index = 1; index < messages.size(); index++) {
                submit.message(messages.get(index));
            }
            submit.submit();
            submitted = true;
        } finally {
            if (!submitted) {
                messages.forEach(Message::close);
            }
        }
    }

    SocketMonitor openMonitor(
        RouterSocket router,
        MonitorEventType... eventTypes) {
        ensureOwned(router);
        return router.monitorOpen(eventTypes);
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
            return Optional.of(new Inbound(
                source,
                received.requestSeq().orElse(null),
                frames));
        }
    }

    @Override
    public synchronized void close() {
        if (!closed.compareAndSet(false, true)) {
            return;
        }
        Poller currentCompletionPoller = completionPoller;
        if (currentCompletionPoller != null) {
            currentCompletionPoller.close();
        }
        completionPoller = null;
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

    record Inbound(
        RoutingId source,
        Long requestSequence,
        List<byte[]> frames) {
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
