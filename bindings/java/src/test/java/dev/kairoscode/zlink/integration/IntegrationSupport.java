package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.TestSupport;
import dev.kairoscode.zlink.Zlink;
import org.junit.jupiter.api.Assumptions;

import java.util.ArrayList;
import java.util.List;

final class IntegrationSupport {
    private IntegrationSupport() {
    }

    static List<String> coreTransports() {
        List<String> out = new ArrayList<>();
        out.add("tcp");
        out.add("inproc");
        if (has("ws"))
            out.add("ws");
        return out;
    }

    static String endpointFor(String transport, String tag) {
        return switch (transport) {
            case "tcp" -> TestSupport.tcpEndpoint();
            case "ws" -> TestSupport.wsEndpoint();
            case "inproc" -> TestSupport.inprocEndpoint(tag);
            default -> throw new IllegalArgumentException("unknown transport: " + transport);
        };
    }

    static void requireCapability(String cap) {
        Assumptions.assumeTrue(has(cap), "transport not available: " + cap);
    }

    private static boolean has(String cap) {
        try {
            return Zlink.has(cap);
        } catch (RuntimeException ex) {
            return false;
        }
    }
}
