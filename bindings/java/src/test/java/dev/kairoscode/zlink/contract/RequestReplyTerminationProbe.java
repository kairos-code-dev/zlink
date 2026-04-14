/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.contract;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.DealerSocket;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.Received;
import dev.kairoscode.zlink.RouterSocket;
import dev.kairoscode.zlink.TestSupport;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;

public final class RequestReplyTerminationProbe {
    private static final Path LOG_PATH = Path.of(
        System.getProperty("zlink.reqrep.probe.log",
            "/tmp/zlink-reqrep-probe-" + ProcessHandle.current().pid() + ".log"));

    private RequestReplyTerminationProbe() {
    }

    public static void main(String[] args) throws Exception {
        TestSupport.assumeNative();
        boolean callbackMode = args.length > 0 && "callback".equals(args[0]);
        Files.deleteIfExists(LOG_PATH);
        log("start callbackMode=" + callbackMode);

        try (Context ctx = new Context();
             RouterSocket routerSocket = new RouterSocket(ctx);
             DealerSocket dealerSocket = new DealerSocket(ctx);
             ExecutorService serverExecutor = daemonExecutor("zlink-reqrep-probe")) {
            String endpoint = TestSupport.inprocEndpoint(
                "request-reply-exit-regression");
            log("bind");
            routerSocket.bind(endpoint);
            log("connect");
            dealerSocket.connect(endpoint);

            CompletableFuture<Void> server;
            if (callbackMode) {
                log("server callback install");
                CountDownLatch handled = new CountDownLatch(1);
                CompletableFuture<Void> callbackFuture = new CompletableFuture<>();
                routerSocket.onReceive(received -> {
                    log("server callback recv");
                    try (received) {
                        if (!received.requestSeq().isPresent()) {
                            throw new IllegalStateException("missing request seq");
                        }
                        log("server callback reply");
                        received.reply(List.of(Message.copyOfUtf8("pong")));
                        callbackFuture.complete(null);
                    } catch (Throwable error) {
                        callbackFuture.completeExceptionally(error);
                    } finally {
                        handled.countDown();
                    }
                });
                server = CompletableFuture.runAsync(() -> {
                    try {
                        log("server callback await");
                        if (!handled.await(2, TimeUnit.SECONDS)) {
                            throw new IllegalStateException("router callback timed out");
                        }
                        callbackFuture.get(2, TimeUnit.SECONDS);
                        log("server callback done");
                    } catch (Exception ex) {
                        throw new IllegalStateException(ex);
                    }
                }, serverExecutor);
            } else {
                server = CompletableFuture.runAsync(() -> {
                    log("server recv wait");
                    try (Received received = routerSocket.recv()) {
                        log("server recv got");
                        if (!received.requestSeq().isPresent()) {
                            throw new IllegalStateException("missing request seq");
                        }
                        log("server recv reply");
                        received.reply(List.of(Message.copyOfUtf8("pong")));
                    }
                }, serverExecutor);
            }

            try (Message request = Message.copyOfUtf8("ping")) {
                log("dealer request begin");
                List<Message> reply = dealerSocket.request(request,
                    Duration.ofSeconds(2)).get(2, TimeUnit.SECONDS);
                try {
                    log("dealer request complete");
                    byte[] payload = reply.get(0).toByteArray();
                    String text = new String(payload, StandardCharsets.UTF_8);
                    if (!"pong".equals(text)) {
                        throw new IllegalStateException(
                            "unexpected reply: " + text);
                    }
                } finally {
                    Message.closeAll(reply);
                }
            }

            log("server future wait");
            server.get(2, TimeUnit.SECONDS);
            log("scope end");
        }
        logThreads();
        Thread observer = new Thread(() -> {
            try {
                Thread.sleep(1_000L);
                log("post-main observer");
                logThreads();
            } catch (InterruptedException ignored) {
                Thread.currentThread().interrupt();
            }
        }, "zlink-reqrep-post-main");
        observer.setDaemon(true);
        observer.start();
        log("main end");
    }

    private static void log(String message) {
        try {
            Files.writeString(LOG_PATH, message + System.lineSeparator(),
                StandardOpenOption.CREATE, StandardOpenOption.APPEND);
        } catch (Exception ignored) {
        }
    }

    private static ExecutorService daemonExecutor(String name) {
        return Executors.newSingleThreadExecutor(runnable -> {
            Thread thread = new Thread(runnable, name);
            thread.setDaemon(true);
            return thread;
        });
    }

    private static void logThreads() {
        for (Thread thread : Thread.getAllStackTraces().keySet()) {
            log("thread " + thread.getName() + " daemon=" + thread.isDaemon()
                + " state=" + thread.getState());
        }
    }
}
