/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.eventing;

import java.util.HashMap;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.errors.ZlinkException;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.sockets.DealerSocket;
import systems.zlink.contracts.sockets.PairSocket;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.contracts.sockets.RouterSocket;
import systems.zlink.contracts.sockets.Socket;

final class ZlinkPollNativeDispatcher implements ZlinkNativeDispatcher {
    private final ZlinkDispatchOptions options;
    private final Object gate = new Object();
    private final AtomicLong nextSlot = new AtomicLong(1);
    private final Map<Long, Registration> registrationsBySlot = new HashMap<>();
    private final Poller poller;
    private final PollEvents events;
    private final AtomicBoolean running = new AtomicBoolean();
    private volatile CompletableFuture<Void> stopFuture = CompletableFuture.completedFuture(null);
    private Thread runtimeThread;
    private boolean closed;

    ZlinkPollNativeDispatcher(ZlinkDispatchOptions options) {
        this.options = Objects.requireNonNull(options, "options");
        this.poller = Zlink.createPoller();
        this.events = new PollEvents(Math.max(1, options.maxBatchSize()));
    }

    @Override
    public ZlinkDispatcherRegistration register(
        PairSocket socket,
        ZlinkReceiveHandler handler) {
        return registerSocket(socket, target -> socket.recv(target, RecvFlags.DONT_WAIT), handler);
    }

    @Override
    public ZlinkDispatcherRegistration register(
        DealerSocket socket,
        ZlinkReceiveHandler handler) {
        return registerSocket(socket, target -> socket.recv(target, RecvFlags.DONT_WAIT), handler);
    }

    @Override
    public ZlinkDispatcherRegistration register(
        RouterSocket socket,
        ZlinkReceiveHandler handler) {
        return registerSocket(socket, target -> socket.recv(target, RecvFlags.DONT_WAIT), handler);
    }

    @Override
    public CompletionStage<Void> start() {
        synchronized (gate) {
            ensureOpen();
            if (running.get()) {
                return stopFuture;
            }
            CompletableFuture<Void> completion = new CompletableFuture<>();
            stopFuture = completion;
            running.set(true);
            runtimeThread = Thread.ofPlatform()
                .name("zlink-native-dispatcher")
                .daemon(true)
                .start(() -> runLoop(completion));
            return CompletableFuture.completedFuture(null);
        }
    }

    @Override
    public CompletionStage<Void> stop() {
        Thread thread;
        synchronized (gate) {
            running.set(false);
            thread = runtimeThread;
        }
        if (thread != null) {
            thread.interrupt();
        }
        return stopFuture;
    }

    @Override
    public boolean isRunning() {
        return running.get();
    }

    @Override
    public void close() {
        stop().toCompletableFuture().join();
        synchronized (gate) {
            if (closed) {
                return;
            }
            registrationsBySlot.values().forEach(Registration::markClosed);
            registrationsBySlot.clear();
            poller.close();
            closed = true;
        }
    }

    private ZlinkDispatcherRegistration registerSocket(
        Socket socket,
        Receiver receiver,
        ZlinkReceiveHandler handler) {
        Objects.requireNonNull(socket, "socket");
        Objects.requireNonNull(receiver, "receiver");
        Objects.requireNonNull(handler, "handler");
        synchronized (gate) {
            ensureOpen();
            long slot = nextSlot.getAndIncrement();
            Registration registration = new Registration(slot, socket, receiver, handler);
            registrationsBySlot.put(slot, registration);
            poller.add(socket, slot, PollEventFlags.POLLIN);
            return registration;
        }
    }

    private void runLoop(CompletableFuture<Void> completion) {
        try {
            while (running.get()) {
                int count;
                synchronized (gate) {
                    if (!running.get()) {
                        break;
                    }
                    count = poller.wait(events, options.pollTimeout());
                }
                for (int i = 0; i < count; i++) {
                    Registration registration;
                    synchronized (gate) {
                        registration = registrationsBySlot.get(events.slot(i));
                    }
                    if (registration != null && !registration.isClosed()) {
                        drain(registration);
                    }
                }
            }
            completion.complete(null);
        } catch (Throwable ex) {
            running.set(false);
            completion.completeExceptionally(ex);
        }
    }

    private void drain(Registration registration) {
        for (int i = 0; i < options.maxBatchSize() && running.get(); i++) {
            Received received = new Received();
            boolean hasMessage;
            try {
                hasMessage = registration.receiver.recv(received);
            } catch (ZlinkException ex) {
                received.close();
                if (options.failFastOnClosedHandle()) {
                    throw ex;
                }
                registration.close();
                return;
            } catch (RuntimeException ex) {
                received.close();
                throw ex;
            }
            if (!hasMessage) {
                received.close();
                return;
            }
            options.callbackExecutor().execute(() -> {
                try {
                    registration.handler.handle(received)
                        .whenComplete((ignored, error) -> {
                            if (error != null && options.failFastOnClosedHandle()) {
                                running.set(false);
                            }
                        });
                } catch (RuntimeException ex) {
                    received.close();
                    if (options.failFastOnClosedHandle()) {
                        running.set(false);
                    }
                }
            });
        }
    }

    private void ensureOpen() {
        if (closed) {
            throw new IllegalStateException("dispatcher is closed");
        }
    }

    @FunctionalInterface
    private interface Receiver {
        boolean recv(Received target);
    }

    private final class Registration implements ZlinkDispatcherRegistration {
        private final long slot;
        private final Socket socket;
        private final Receiver receiver;
        private final ZlinkReceiveHandler handler;
        private final AtomicBoolean registrationClosed = new AtomicBoolean();

        private Registration(
            long slot,
            Socket socket,
            Receiver receiver,
            ZlinkReceiveHandler handler) {
            this.slot = slot;
            this.socket = socket;
            this.receiver = receiver;
            this.handler = handler;
        }

        @Override
        public boolean isClosed() {
            return registrationClosed.get();
        }

        @Override
        public void close() {
            if (!registrationClosed.compareAndSet(false, true)) {
                return;
            }
            synchronized (gate) {
                registrationsBySlot.remove(slot);
                poller.remove(socket);
            }
        }

        private void markClosed() {
            registrationClosed.set(true);
        }
    }
}
