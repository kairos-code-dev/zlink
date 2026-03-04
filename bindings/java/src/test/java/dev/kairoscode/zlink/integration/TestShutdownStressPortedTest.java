package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.ContextOption;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.FutureTask;

public class TestShutdownStressPortedTest {
    private static final int THREAD_COUNT = 100;

    @Test
    public void testShutdownStress() throws Exception {
        TestSupport.assumeNative();

        for (int round = 0; round < 10; round++) {
            try (Context ctx = new Context()) {
                ctx.setOption(ContextOption.IO_THREADS, 7);
                String endpoint;
                try (Socket pub = new Socket(ctx, SocketType.PUB)) {
                    endpoint = TestSupport.tcpEndpoint();
                    pub.bind(endpoint);

                    List<Thread> workers = new ArrayList<>(THREAD_COUNT);
                    List<FutureTask<Void>> tasks = new ArrayList<>(THREAD_COUNT);
                    for (int i = 0; i < THREAD_COUNT; i++) {
                        FutureTask<Void> task = new FutureTask<>(() -> {
                            try (Socket sub = new Socket(ctx, SocketType.SUB)) {
                                sub.connect(endpoint);
                            }
                            return null;
                        });
                        Thread thread = new Thread(task, "shutdown-stress-" + i);
                        thread.setDaemon(true);
                        workers.add(thread);
                        tasks.add(task);
                        thread.start();
                    }

                    for (FutureTask<Void> task : tasks) {
                        task.get();
                    }
                }
            }
        }
    }
}
