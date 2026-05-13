package systems.zlink.contract;

import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.regex.Pattern;
import org.junit.jupiter.api.Test;

final class OptimizationGuardContractTest {
    private static final Path MAIN_SOURCE = Path.of("src", "main", "java");

    private static final String[] AGGREGATE_SYMBOLS = {
        "zlink_send",
        "zlink_recv",
        "zlink_publish",
        "zlink_subscribe",
        "zlink_router_recv",
        "zlink_dealer_request",
        "zlink_router_request",
        "zlink_router_reply",
        "zlink_spot_send_channel",
        "zlink_spot_request_channel",
        "zlink_spot_request_spot",
        "zlink_spot_request_router",
        "zlink_spot_publish",
        "zlink_spot_subscribe",
        "zlink_spot_send_spot",
        "zlink_spot_reply_spot",
        "zlink_spot_reply_router",
        "zlink_spot_recv"
    };

    private static final String[] REQUIRED_PART_SYMBOLS = {
        "zlink_send_part",
        "zlink_recv_part",
        "zlink_publish_part",
        "zlink_subscribe_part",
        "zlink_router_recv_part",
        "zlink_dealer_request_part",
        "zlink_router_request_part",
        "zlink_router_reply_part",
        "zlink_spot_publish_part",
        "zlink_spot_subscribe_part",
        "zlink_spot_request_channel_part",
        "zlink_spot_request_spot_part",
        "zlink_spot_reply_router_part"
    };

    @Test
    void hotPathsUsePartSubstrateInsteadOfAggregateSymbols() throws IOException {
        String source = allMainSource();

        for (String symbol : REQUIRED_PART_SYMBOLS) {
            assertTrue(source.contains(symbol), "missing helper substrate " + symbol);
        }

        List<String> violations = new ArrayList<>();
        for (String symbol : AGGREGATE_SYMBOLS) {
            Pattern exactSymbolString = Pattern.compile("\"" + Pattern.quote(symbol) + "\"");
            if (exactSymbolString.matcher(source).find()) {
                violations.add(symbol);
            }
        }

        assertTrue(violations.isEmpty(), "aggregate hot-path symbols found: " + violations);
    }

    @Test
    void runtimeSourceDoesNotUseReflectionOrNativeAccessWorkarounds() throws IOException {
        String source = allMainSource();
        String sourceWithoutUnsafeBootstrap = source
            .replace("Unsafe.class.getDeclaredField(\"theUnsafe\")", "")
            .replace("field.setAccessible(true)", "");

        assertFalse(sourceWithoutUnsafeBootstrap.contains("setAccessible(true)"));
        assertFalse(sourceWithoutUnsafeBootstrap.contains("getDeclaredMethod("));
        assertFalse(sourceWithoutUnsafeBootstrap.contains("getDeclaredField("));
        assertFalse(sourceWithoutUnsafeBootstrap.contains("Class.forName(\"com.sun.jna"));
        assertFalse(sourceWithoutUnsafeBootstrap.contains("com.sun.jna"));
    }

    private static String allMainSource() throws IOException {
        StringBuilder builder = new StringBuilder();
        try (var stream = Files.walk(MAIN_SOURCE)) {
            for (Path path : stream.filter(p -> p.toString().endsWith(".java")).toList()) {
                builder.append(Files.readString(path, StandardCharsets.UTF_8)).append('\n');
            }
        }
        return builder.toString();
    }
}
