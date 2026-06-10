package systems.zlink.samples.bingo.probe;

import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.URI;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.locks.LockSupport;
import systems.zlink.samples.bingo.shared.configuration.SampleTopology;

public final class Program {
    private Program() {
    }

    public static void main(String[] args) {
        Duration timeout = Duration.ofSeconds(Integer.parseInt(readOption(args, "--timeout-seconds", "10")));
        long deadline = System.nanoTime() + timeout.toNanos();
        List<String> endpoints = List.of(
            SampleTopology.RegistryPubEndpoint,
            SampleTopology.RegistryRouterEndpoint,
            SampleTopology.ApiChannelEndpoint,
            SampleTopology.PlayChannelEndpoint,
            SampleTopology.SessionSpotEndpoint,
            SampleTopology.SessionRouterEndpoint,
            SampleTopology.PlaySpotEndpoint,
            SampleTopology.PlaySpotRouterEndpoint,
            SampleTopology.StreamEndpoint);
        while (System.nanoTime() < deadline) {
            if (endpoints.stream().allMatch(Program::canConnect)) {
                System.out.println("topology=ready");
                return;
            }
            LockSupport.parkNanos(Duration.ofMillis(100).toNanos());
        }
        throw new IllegalStateException("Timed out waiting for Bingo sample topology readiness.");
    }

    private static boolean canConnect(String endpoint) {
        URI uri = URI.create(endpoint);
        try (Socket socket = new Socket()) {
            socket.connect(new InetSocketAddress(uri.getHost(), uri.getPort()), 100);
            return true;
        } catch (Exception ex) {
            return false;
        }
    }

    private static String readOption(String[] args, String name, String defaultValue) {
        for (int index = 0; index + 1 < args.length; index++) {
            if (name.equals(args[index])) {
                return args[index + 1];
            }
        }
        return defaultValue;
    }
}
