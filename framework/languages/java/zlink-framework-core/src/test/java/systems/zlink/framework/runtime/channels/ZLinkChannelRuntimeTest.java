package systems.zlink.framework.runtime.channels;

import static org.junit.jupiter.api.Assertions.assertSame;

import java.lang.reflect.Method;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.channels.ZLinkRequestContext;

final class ZLinkChannelRuntimeTest {
    @Test
    void methodArgumentsBindMessageContextAndCancellationTokenLikeDotnet() throws Exception {
        DefaultRequestContext context = new DefaultRequestContext();
        Method method = ContextHandler.class.getMethod(
            "handle",
            String.class,
            ZLinkRequestContext.class,
            CancellationToken.class);

        Object[] arguments = ZLinkChannelRuntime.methodArguments(method, "hello", context);

        assertSame("hello", arguments[0]);
        assertSame(context, arguments[1]);
        assertSame(context.cancellationToken(), arguments[2]);
    }

    public static final class ContextHandler {
        public void handle(
            String request,
            ZLinkRequestContext context,
            CancellationToken cancellationToken) {
        }
    }

    private static final class DefaultRequestContext implements ZLinkRequestContext {
        private static final CancellationToken NONE = () -> false;

        @Override
        public java.util.Optional<String> channelName() {
            return java.util.Optional.of("profile");
        }

        @Override
        public java.util.Optional<String> packetName() {
            return java.util.Optional.of("Echo");
        }

        @Override
        public java.util.Optional<String> contentType() {
            return java.util.Optional.empty();
        }

        @Override
        public CancellationToken cancellationToken() {
            return NONE;
        }
    }
}
