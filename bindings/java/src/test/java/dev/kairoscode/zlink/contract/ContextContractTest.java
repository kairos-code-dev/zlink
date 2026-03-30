package dev.kairoscode.zlink.contract;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.TestSupport;
import java.lang.reflect.Method;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class ContextContractTest {
    @Test
    public void rawContextOptionBagIsHiddenAndTypedSurfaceRemains() {
        TestSupport.assumeNative();

        try (Context ctx = new Context()) {
            assertFalse(hasPublicMethod(Context.class, "setOption"));
            assertFalse(hasPublicMethod(Context.class, "getOption"));
            assertDoesNotThrow(() -> ctx.ioThreads(2));
            assertEquals(2, ctx.ioThreads());
            assertTrue(ctx.socketLimit() >= ctx.maxSockets());
            assertDoesNotThrow(() -> ctx.blocky(true));
            assertTrue(ctx.blocky());
            assertDoesNotThrow(() -> ctx.blocky(false));
            assertFalse(ctx.blocky());
            assertTrue(ctx.messageStructSize() > 0);
        }
    }

    private static boolean hasPublicMethod(Class<?> type, String name) {
        for (Method method : type.getMethods()) {
            if (method.getName().equals(name))
                return true;
        }
        return false;
    }
}
