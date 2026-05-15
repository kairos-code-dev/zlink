package systems.zlink.contract;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import systems.zlink.contracts.Context;
import systems.zlink.contracts.ContextOptions;
import systems.zlink.contracts.AutoHwmProfile;
import systems.zlink.contracts.TestSupport;
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
            ContextOptions options = ctx.options();
            assertTrue(hasPublicMethod(Context.class, "options"));
            assertEquals(ContextOptions.class, options.getClass());
            assertFalse(hasPublicMethod(Context.class, "setOption"));
            assertFalse(hasPublicMethod(Context.class, "getOption"));
            assertFalse(hasPublicMethod(Context.class, "ioThreads"));
            assertFalse(hasPublicMethod(Context.class, "maxSockets"));
            assertFalse(hasPublicMethod(Context.class, "threadSchedPolicy"));
            assertFalse(hasPublicMethod(Context.class, "messageStructSize"));
            assertFalse(hasPublicMethod(ContextOptions.class, "addThreadAffinityCpu"));
            assertFalse(hasPublicMethod(ContextOptions.class, "removeThreadAffinityCpu"));
            assertTrue(hasPublicMethod(ContextOptions.class, "addThreadAffinity", int.class));
            assertTrue(hasPublicMethod(ContextOptions.class, "removeThreadAffinity", int.class));
            assertDoesNotThrow(() -> options.ioThreads(2));
            assertEquals(2, options.ioThreads());
            assertTrue(options.socketLimit() >= options.maxSockets());
            assertDoesNotThrow(() -> options.blocky(true));
            assertTrue(options.blocky());
            assertDoesNotThrow(() -> options.blocky(false));
            assertFalse(options.blocky());
            assertDoesNotThrow(
                () -> options.autoHwmProfile(AutoHwmProfile.COMPACT));
            assertEquals(AutoHwmProfile.COMPACT, options.autoHwmProfile());
            assertTrue(options.msgTSize() > 0);
        }
    }

    private static boolean hasPublicMethod(Class<?> type, String name,
                                           Class<?>... parameterTypes) {
        for (Method method : type.getMethods()) {
            if (method.getName().equals(name)
                && java.util.Arrays.equals(method.getParameterTypes(),
                    parameterTypes))
                return true;
        }
        return false;
    }
}
