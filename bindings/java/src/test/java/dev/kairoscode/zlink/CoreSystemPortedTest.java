package dev.kairoscode.zlink;

import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.net.ServerSocket;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;

import static org.junit.jupiter.api.Assertions.assertEquals;

public class CoreSystemPortedTest {
    @Test
    public void testLocalhostNetworkingViaZlink() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket dealer = new Socket(ctx, SocketType.DEALER)) {
            dealer.bind(TestSupport.tcpEndpoint());
        }
    }

    @Test
    public void testPlainSocketCapacityBaseline() throws Exception {
        final int target = 128;

        try (ServerSocket server = new ServerSocket(0)) {
            server.setReuseAddress(true);

            List<java.net.Socket> accepted = new ArrayList<>();
            AtomicBoolean running = new AtomicBoolean(true);
            Thread acceptThread = new Thread(() -> {
                while (running.get()) {
                    try {
                        java.net.Socket socket = server.accept();
                        synchronized (accepted) {
                            accepted.add(socket);
                        }
                    } catch (IOException e) {
                        if (running.get())
                            throw new RuntimeException(e);
                    }
                }
            });
            acceptThread.setDaemon(true);
            acceptThread.start();

            List<java.net.Socket> clients = new ArrayList<>();
            for (int i = 0; i < target; i++) {
                clients.add(new java.net.Socket("127.0.0.1",
                    server.getLocalPort()));
            }

            TestSupport.waitUntil(() -> {
                synchronized (accepted) {
                    return accepted.size() == target;
                }
            }, TestSupport.DEFAULT_TIMEOUT_MS,
                "accepted connection count mismatch");

            assertEquals(target, clients.size());

            for (java.net.Socket client : clients) {
                client.close();
            }
            synchronized (accepted) {
                for (java.net.Socket socket : accepted) {
                    socket.close();
                }
            }

            running.set(false);
            server.close();
            acceptThread.join(500);
        }
    }
}
