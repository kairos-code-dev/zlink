package systems.zlink.contract;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import systems.zlink.contracts.Message;
import systems.zlink.contracts.TestSupport;
import java.lang.reflect.Method;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class MessageDiagnosticsContractTest {
    @Test
    public void messageDiagnosticsUseCanonicalNames() {
        TestSupport.assumeNative();

        try (Message msg = Message.from("diagnostic")) {
            assertTrue(hasPublicMethod(Message.class, "getProperty",
                String.class));
            assertFalse(hasPublicMethod(Message.class, "property",
                String.class));
            assertEquals(1, msg.refCount());
            assertNull(msg.getProperty("Socket-Type"));
        }
    }

    private static boolean hasPublicMethod(Class<?> type, String name,
                                           Class<?>... parameterTypes) {
        for (Method method : type.getMethods()) {
            if (method.getName().equals(name)
                && java.util.Arrays.equals(method.getParameterTypes(),
                  parameterTypes)) {
                return true;
            }
        }
        return false;
    }
}
