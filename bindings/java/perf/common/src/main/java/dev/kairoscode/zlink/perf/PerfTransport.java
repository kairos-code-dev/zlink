/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.MonitorSocket;
import dev.kairoscode.zlink.PerfSocketOptions;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.service.spot.SpotNode;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

final class PerfTransport {
    private PerfTransport() {
    }

    static Context newContext(PerfUtil.Config config) {
        Context ctx = new Context();
        if (config.ioThreads() > 0) {
            ctx.options().ioThreads(config.ioThreads());
        }
        return ctx;
    }

    static void configureServerTls(Socket socket, String transport) {
        if (!isTlsTransport(transport)) {
            return;
        }
        socket.setTlsServer(cert("server.crt"), cert("server.key"), false);
    }

    static void configureClientTls(Socket socket, String transport) {
        if (!isTlsTransport(transport)) {
            return;
        }
        socket.setTlsClient(cert("ca.crt"), "localhost", true);
    }

    static void configureServerTls(SpotNode node, String transport) {
        if (!isTlsTransport(transport)) {
            return;
        }
        node.setTlsServer(cert("server.crt"), cert("server.key"), false);
    }

    static void configureClientTls(SpotNode node, String transport) {
        if (!isTlsTransport(transport)) {
            return;
        }
        node.setTlsClient(cert("ca.crt"), "localhost", true);
    }

    static void applySocketOptions(Socket socket, PerfUtil.Config config) {
        PerfSocketOptions.linger(socket, Duration.ZERO);
        if (config.sendHwm() > 0) {
            PerfSocketOptions.sendHwm(socket, config.sendHwm());
        }
        if (config.recvHwm() > 0) {
            PerfSocketOptions.recvHwm(socket, config.recvHwm());
        }
        if (config.sendTimeoutMs() >= 0) {
            PerfSocketOptions.sendTimeout(socket,
                Duration.ofMillis(config.sendTimeoutMs()));
        }
        if (config.recvTimeoutMs() >= 0) {
            PerfSocketOptions.recvTimeout(socket,
                Duration.ofMillis(config.recvTimeoutMs()));
        }
    }

    static void applyMonitorOptions(MonitorSocket monitor, PerfUtil.Config config) {
        // The aligned MonitorSocket surface does not accept generic HWM tuning.
        // Perf runners keep the hook for parity, but unsupported monitor options
        // must degrade to a no-op instead of failing startup.
    }

    static void applySpotOptions(SpotNode node, PerfUtil.Config config) {
        // SpotNode public API intentionally exposes topology/lifecycle only.
    }

    static void await(CountDownLatch latch, String label, Duration timeout) {
        try {
            if (!latch.await(timeout.toMillis(), TimeUnit.MILLISECONDS)) {
                throw new IllegalStateException(label + " timed out");
            }
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException(label + " interrupted", ex);
        }
    }

    static void join(Thread thread, String label, Duration timeout) {
        try {
            thread.join(timeout.toMillis());
            if (thread.isAlive()) {
                throw new IllegalStateException(label + " timed out");
            }
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException(label + " interrupted", ex);
        }
    }

    static void waitForMonitorEvent(MonitorSocket monitor,
                                    MonitorEventType expectedEvent,
                                    int expectedCount, Duration timeout,
                                    String label) {
        CountDownLatch done = new CountDownLatch(1);
        AtomicReference<Throwable> failure = new AtomicReference<>();
        Thread waiter = new Thread(() -> {
            try {
                int seen = 0;
                while (seen < expectedCount) {
                    var event = monitor.recv();
                    if (event.event() != expectedEvent) {
                        continue;
                    }
                    seen++;
                }
            } catch (Throwable ex) {
                failure.compareAndSet(null, ex);
            } finally {
                done.countDown();
            }
        }, "perf-monitor-wait");
        waiter.setDaemon(true);
        waiter.start();
        await(done, label, timeout);
        Throwable error = failure.get();
        if (error != null) {
            throw new IllegalStateException(label + " failed", error);
        }
    }

    private static boolean isTlsTransport(String transport) {
        return "tls".equals(transport) || "wss".equals(transport);
    }

    private static String cert(String name) {
        Path cwd = Path.of("").toAbsolutePath().normalize();
        Path current = cwd;
        while (current != null) {
            Path candidate = current.resolve("tests").resolve("certs").resolve(name);
            if (Files.exists(candidate)) {
                return candidate.toAbsolutePath().toString();
            }
            current = current.getParent();
        }
        return cwd.resolve("tests").resolve("certs").resolve(name)
            .toAbsolutePath().toString();
    }
}
