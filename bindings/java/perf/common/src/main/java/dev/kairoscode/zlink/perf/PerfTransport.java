/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf;

import dev.kairoscode.zlink.MonitorEvent;
import dev.kairoscode.zlink.MonitorSocket;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.service.spot.SpotNode;
import java.net.ServerSocket;
import java.time.Duration;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

final class PerfTransport {
    private PerfTransport() {
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

    static void waitForMonitorEvent(MonitorSocket monitor, long expectedMask,
                                    int expectedCount, Duration timeout,
                                    String label) {
        final RuntimeException[] failure = new RuntimeException[1];
        Thread waiter = new Thread(() -> {
            int seen = 0;
            while (seen < expectedCount) {
                MonitorEvent event = monitor.recv();
                if ((event.event() & expectedMask) == 0L) {
                    failure[0] = new IllegalStateException(label
                        + " unexpected event: " + event.event());
                    return;
                }
                seen++;
            }
        }, "perf-monitor-wait");
        waiter.setDaemon(true);
        waiter.start();
        join(waiter, label, timeout);
        if (failure[0] != null) {
            throw failure[0];
        }
    }

    static void waitForReadySignal(int port) {
        if (port <= 0) {
            return;
        }
        try (ServerSocket server = new ServerSocket(port);
             java.net.Socket socket = server.accept()) {
            socket.setSoTimeout(10_000);
            if (socket.getInputStream().read() < 0) {
                throw new IllegalStateException("ready signal closed");
            }
        } catch (java.io.IOException ex) {
            throw new IllegalStateException("failed waiting for ready signal", ex);
        }
    }

    static void sendReadySignal(int port) {
        if (port <= 0) {
            return;
        }
        long deadline = System.nanoTime() + Duration.ofSeconds(10).toNanos();
        while (System.nanoTime() < deadline) {
            try (java.net.Socket socket = new java.net.Socket("127.0.0.1", port)) {
                socket.getOutputStream().write(1);
                socket.getOutputStream().flush();
                return;
            } catch (java.io.IOException ignored) {
                try {
                    Thread.sleep(25L);
                } catch (InterruptedException ex) {
                    Thread.currentThread().interrupt();
                    throw new IllegalStateException("ready signal interrupted", ex);
                }
            }
        }
        throw new IllegalStateException("failed to send ready signal");
    }

    private static boolean isTlsTransport(String transport) {
        return "tls".equals(transport) || "wss".equals(transport);
    }

    private static String cert(String name) {
        return java.nio.file.Path.of("tests", "certs", name).toAbsolutePath().toString();
    }
}
