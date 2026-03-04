package dev.kairoscode.zlink;

import dev.kairoscode.zlink.options.SocketOptionKey;
import dev.kairoscode.zlink.options.SocketOptions;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assertions.fail;

public class SocketOptionsTypeMapTest {
    @Test
    public void catalogCoversSocketOptionEnum() {
        List<SocketOptionKey<?>> keys = SocketOptions.all();
        assertFalse(keys.isEmpty());

        Map<Integer, Integer> idCounts = new HashMap<>();
        Map<Integer, Set<String>> idToNames = new HashMap<>();
        Set<String> names = new HashSet<>();
        for (SocketOptionKey<?> key : keys) {
            assertTrue(names.add(key.name()), "duplicate key name: " + key.name());
            idCounts.merge(key.optionId(), 1, Integer::sum);
            idToNames.computeIfAbsent(key.optionId(), ignored -> new HashSet<>())
                .add(key.name());
            assertTrue(key.readable() || key.writable(),
                "invalid key access mode: " + key.name());
        }

        for (SocketOption option : SocketOption.values()) {
            assertTrue(idCounts.containsKey(option.getValue()),
                "missing mapping for enum option: " + option.name());
        }

        int aliasId = SocketOption.TLS_VERIFY.getValue();
        for (Map.Entry<Integer, Set<String>> entry : idToNames.entrySet()) {
            Set<String> mapped = entry.getValue();
            if (mapped.size() <= 1)
                continue;
            if (entry.getKey() == aliasId) {
                assertEquals(Set.of("TLS_VERIFY", "XPUB_MANUAL_LAST_VALUE"),
                    mapped, "unexpected alias mapping for option id 98");
                continue;
            }
            if (isStringBytesTwin(mapped))
                continue;
            fail("unexpected duplicate option id " + entry.getKey()
                + ": " + mapped);
        }
    }

    @Test
    public void stringFirstAndBytesTwinKeysAreConfigured() {
        assertEquals(String.class, SocketOptions.ROUTING_ID.valueClass());
        assertEquals(byte[].class, SocketOptions.ROUTING_ID_BYTES.valueClass());
        assertEquals(SocketOptions.ROUTING_ID.optionId(),
            SocketOptions.ROUTING_ID_BYTES.optionId());

        assertEquals(String.class, SocketOptions.CONNECT_ROUTING_ID.valueClass());
        assertEquals(byte[].class,
            SocketOptions.CONNECT_ROUTING_ID_BYTES.valueClass());
        assertEquals(SocketOptions.CONNECT_ROUTING_ID.optionId(),
            SocketOptions.CONNECT_ROUTING_ID_BYTES.optionId());

        assertEquals(String.class, SocketOptions.SUBSCRIBE.valueClass());
        assertEquals(byte[].class, SocketOptions.SUBSCRIBE_BYTES.valueClass());
        assertEquals(SocketOptions.SUBSCRIBE.optionId(),
            SocketOptions.SUBSCRIBE_BYTES.optionId());
    }

    private static boolean isStringBytesTwin(Set<String> mapped) {
        if (mapped.size() != 2)
            return false;
        String[] names = mapped.toArray(String[]::new);
        return isBytesTwin(names[0], names[1]) || isBytesTwin(names[1], names[0]);
    }

    private static boolean isBytesTwin(String maybeBase, String maybeBytes) {
        if (!maybeBytes.endsWith("_BYTES"))
            return false;
        String base = maybeBytes.substring(0, maybeBytes.length() - "_BYTES".length());
        return base.equals(maybeBase);
    }
}
