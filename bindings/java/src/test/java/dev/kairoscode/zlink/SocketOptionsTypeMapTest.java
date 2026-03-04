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

public class SocketOptionsTypeMapTest {
    @Test
    public void catalogCoversSocketOptionEnum() {
        List<SocketOptionKey<?>> keys = SocketOptions.all();
        assertFalse(keys.isEmpty());

        Map<Integer, Integer> idCounts = new HashMap<>();
        Set<String> names = new HashSet<>();
        for (SocketOptionKey<?> key : keys) {
            assertTrue(names.add(key.name()), "duplicate key name: " + key.name());
            idCounts.merge(key.optionId(), 1, Integer::sum);
            assertTrue(key.readable() || key.writable(),
                "invalid key access mode: " + key.name());
        }

        for (SocketOption option : SocketOption.values()) {
            assertTrue(idCounts.containsKey(option.getValue()),
                "missing mapping for enum option: " + option.name());
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
}
