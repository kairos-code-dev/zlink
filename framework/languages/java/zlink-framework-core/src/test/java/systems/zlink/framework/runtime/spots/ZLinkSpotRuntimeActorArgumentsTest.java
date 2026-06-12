package systems.zlink.framework.runtime.spots;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertSame;

import java.lang.reflect.Method;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.framework.spots.ZLinkSpotContext;

final class ZLinkSpotRuntimeActorArgumentsTest {
    @Test
    void bindsDotnetShapeSpotActorRequestArguments() throws Exception {
        Method method = DotnetShapeHandler.class.getMethod(
            "request",
            TestSpot.class,
            TestActor.class,
            ZLinkSpotActorRequestContext.class,
            Request.class,
            CancellationToken.class);
        TestSpot spot = new TestSpot();
        TestActor actor = new TestActor();
        Request request = new Request();
        ZLinkSpotActorRequestContext context = requestContext("PlaceMarkReq");

        Object[] args = ZLinkSpotRuntime.actorPacketArguments(
            method,
            spot,
            actor,
            context,
            request);

        assertSame(spot, args[0]);
        assertSame(actor, args[1]);
        assertSame(context, args[2]);
        assertSame(request, args[3]);
        assertSame(context.cancellationToken(), args[4]);
    }

    private static ZLinkSpotActorRequestContext requestContext(String packetName) {
        return new ZLinkSpotActorRequestContext() {
            private final CancellationToken token = () -> false;

            @Override
            public Optional<String> channelName() {
                return Optional.empty();
            }

            @Override
            public Optional<String> packetName() {
                return Optional.of(packetName);
            }

            @Override
            public Optional<String> contentType() {
                return Optional.empty();
            }

            @Override
            public CancellationToken cancellationToken() {
                return token;
            }
        };
    }

    public static final class DotnetShapeHandler {
        public Reply request(
            TestSpot spot,
            TestActor actor,
            ZLinkSpotActorRequestContext context,
            Request request,
            CancellationToken cancellationToken) {
            return new Reply();
        }
    }

    public static final class TestSpot implements ZLinkSpot {
        @Override
        public ZLinkSpotContext context() {
            throw new UnsupportedOperationException();
        }
    }

    public static final class TestActor implements ZLinkActor {
        @Override
        public String actorId() {
            return "actor";
        }

        @Override
        public ZLinkActorContext context() {
            throw new UnsupportedOperationException();
        }
    }

    public record Request() {
    }

    public record Reply() {
    }
}
