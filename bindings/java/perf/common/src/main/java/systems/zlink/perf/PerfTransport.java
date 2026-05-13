/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf;

import systems.zlink.AutoHwmProfile;
import systems.zlink.Context;
import systems.zlink.MonitorEventType;
import systems.zlink.MonitorSocket;
import systems.zlink.Socket;
import systems.zlink.service.discovery.Discovery;
import systems.zlink.service.registry.Registry;
import systems.zlink.service.spot.SpotNode;
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
        ctx.options().blocky(benchCtxBlocky());
        ctx.options().autoHwmEnabled(true);
        ctx.options().autoHwmProfile(benchCtxAutoHwmProfile());
        return ctx;
    }

    static boolean manualSocketOverridesEnabled() {
        return PerfUtil.intEnv("PERF_MULTI_ALLOW_MANUAL_SOCKET_OVERRIDES", 0) > 0
            || PerfUtil.intEnv("PERF_ALLOW_MANUAL_SOCKET_OVERRIDES", 0) > 0;
    }

    private static boolean benchCtxBlocky() {
        return PerfUtil.intEnv("PERF_CTX_BLOCKY", 0) != 0;
    }

    private static AutoHwmProfile benchCtxAutoHwmProfile() {
        String value = System.getenv("PERF_CTX_AUTO_HWM_PROFILE");
        if (value == null || value.isBlank()) {
            value = System.getenv("PERF_AUTO_HWM_PROFILE");
        }
        if (value == null || value.isBlank()) {
            return AutoHwmProfile.BALANCED;
        }
        return switch (value.toLowerCase(java.util.Locale.ROOT)) {
            case "compact" -> AutoHwmProfile.COMPACT;
            case "low_latency", "low-latency" -> AutoHwmProfile.LOW_LATENCY;
            case "throughput" -> AutoHwmProfile.THROUGHPUT;
            default -> AutoHwmProfile.BALANCED;
        };
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

    static void configureServerTls(Registry registry, String transport) {
        if (!isTlsTransport(transport)) {
            return;
        }
        registry.setTlsServer(cert("server.crt"), cert("server.key"), false);
    }

    static void configureClientTls(Discovery discovery, String transport) {
        if (!isTlsTransport(transport)) {
            return;
        }
        discovery.setTlsClient(cert("ca.crt"), "localhost", true);
    }

    static void applySocketOptions(Socket socket, PerfUtil.Config config) {
        socket.options().linger(Duration.ZERO);
        if ("multi".equals(config.suite())) {
            if (manualSocketOverridesEnabled()) {
                int sndhwm = config.sendHwm() > 0 ? config.sendHwm() : 1;
                int rcvhwm = config.recvHwm() > 0 ? config.recvHwm() : 1;
                socket.options().sendHwm(sndhwm);
                socket.options().recvHwm(rcvhwm);
            }
        } else {
            if (config.sendHwm() > 0) {
                socket.options().sendHwm(config.sendHwm());
            }
            if (config.recvHwm() > 0) {
                socket.options().recvHwm(config.recvHwm());
            }
        }
        if (config.sendTimeoutMs() >= 0) {
            socket.options().sendTimeout(Duration.ofMillis(config.sendTimeoutMs()));
        }
        if (config.recvTimeoutMs() >= 0) {
            socket.options().recvTimeout(Duration.ofMillis(config.recvTimeoutMs()));
        }
    }

    static void applyAutoHwmMsgUnit(Socket socket, int msgSize) {
        if (msgSize <= 0) {
            return;
        }
        socket.options().autoHwmMessageUnitBytes(msgSize);
    }

    static void recalculateAutoHwm(Context ctx) {
        ctx.recalculateAutoHwm();
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
