package dev.kairoscode.zlink.perf.multi;

import org.junit.jupiter.api.Test;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.ServerSocket;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.TimeUnit;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

class PerfMultiDealerDealerRegressionTest {
    @Test
    void splitProcessServerAndClientCompleteWithoutTimeout() throws Exception {
        String endpoint = "tcp://127.0.0.1:" + freePort();
        String controlPort = String.valueOf(freePort());

        Process server = startPerfProcess(List.of(
            "--multi-server", "MULTI_DEALER_DEALER", "tcp", "64",
            "--endpoint", endpoint,
            "--clients", "1",
            "--duration", "1",
            "--control-port", controlPort
        ));
        Thread.sleep(300L);

        Process client = startPerfProcess(List.of(
            "--multi-client", "MULTI_DEALER_DEALER", "tcp", "64",
            "--endpoint", endpoint,
            "--clients", "1",
            "--duration", "1",
            "--control-port", controlPort
        ));

        String clientOut = waitAndRead(client, Duration.ofSeconds(10), "client");
        String serverOut = waitAndRead(server, Duration.ofSeconds(10), "server");

        assertTrue(clientOut.contains("RESULT,current,DEALER_DEALER,tcp,64,throughput,"),
            "unexpected client output:\n" + clientOut);
        assertTrue(serverOut.contains("RESULT,current,DEALER_DEALER,tcp,64,throughput,"),
            "unexpected server output:\n" + serverOut);
    }

    private static Process startPerfProcess(List<String> args) throws Exception {
        String javaBin = Path.of(System.getProperty("java.home"), "bin", "java").toString();
        String classPath = System.getProperty("java.class.path");
        ProcessBuilder pb = new ProcessBuilder();
        pb.command().add(javaBin);
        pb.command().add("--enable-native-access=ALL-UNNAMED");
        pb.command().add("-cp");
        pb.command().add(classPath);
        pb.command().add("dev.kairoscode.zlink.perf.multi.PerfMain");
        pb.command().addAll(args);
        pb.redirectErrorStream(true);
        return pb.start();
    }

    private static String waitAndRead(Process process, Duration timeout, String label)
        throws Exception {
        boolean exited = process.waitFor(timeout.toMillis(), TimeUnit.MILLISECONDS);
        if (!exited) {
            process.destroy();
            process.waitFor(1, TimeUnit.SECONDS);
        }
        String output = readAll(process.getInputStream());
        assertTrue(exited, label + " timed out:\n" + output);
        assertEquals(0, process.exitValue(), label + " failed:\n" + output);
        return output;
    }

    private static String readAll(InputStream input) throws Exception {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        input.transferTo(out);
        return out.toString(StandardCharsets.UTF_8);
    }

    private static int freePort() throws Exception {
        try (ServerSocket socket = new ServerSocket(0)) {
            socket.setReuseAddress(true);
            return socket.getLocalPort();
        }
    }
}
