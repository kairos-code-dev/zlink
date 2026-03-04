package dev.kairoscode.zlink;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertNotNull;

public class CoreCapabilitiesPortedTest {
    @Test
    public void testCapabilitiesAndUtilityApi() {
        TestSupport.assumeNative();

        assertDoesNotThrow(() -> Zlink.errno());
        assertNotNull(Zlink.strerror(ErrorCode.EFSM.getValue()));
        assertDoesNotThrow(() -> Zlink.has("inproc"));
        assertDoesNotThrow(() -> Zlink.has("tcp"));
        assertDoesNotThrow(() -> Zlink.sleep(0));
    }
}
