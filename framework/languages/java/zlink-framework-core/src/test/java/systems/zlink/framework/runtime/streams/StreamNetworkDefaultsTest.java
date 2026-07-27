package systems.zlink.framework.runtime.streams;

import static org.junit.jupiter.api.Assertions.assertEquals;

import org.junit.jupiter.api.Test;

final class StreamNetworkDefaultsTest {
    @Test
    void streamListenerUsesRootHostsUntilOverridden() {
        StreamNodeRegistration registration = new StreamNodeRegistration("gateway");

        StreamBuilders.streamNode(
                registration, "0.0.0.0", "stream.example.test")
            .bind(0);

        assertEquals("tcp://0.0.0.0:0", registration.bindEndpoint());
        assertEquals(
            "tcp://stream.example.test:43130",
            registration.advertisedEndpoint("tcp://0.0.0.0:43130"));
    }
}
