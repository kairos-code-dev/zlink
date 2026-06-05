package systems.zlink.framework.runtime.spots;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.lang.reflect.Method;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandlerKind;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotActorChangeKind;
import systems.zlink.framework.spots.ZLinkSpotActorChangeResult;
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

    @Test
    void bindsDotnetShapeSpotActorLifecycleArguments() throws Exception {
        Method method = DotnetShapeHandler.class.getMethod(
            "joined",
            TestSpot.class,
            TestActor.class,
            ZLinkSpotActorChangeResult.class,
            CancellationToken.class);
        TestSpot spot = new TestSpot();
        TestActor actor = new TestActor();
        ZLinkSpotActorChangeResult result =
            new ZLinkSpotActorChangeResult(ZLinkSpotActorChangeKind.JOIN_ENTRY_SPOT);

        Object[] args = ZLinkSpotRuntime.actorLifecycleArguments(method, spot, actor, result);

        assertSame(spot, args[0]);
        assertSame(actor, args[1]);
        assertSame(result, args[2]);
        assertEquals(false, ((CancellationToken) args[3]).isCancellationRequested());
    }

    @Test
    void skipsDotnetShapeSpotActorLifecycleHandlerForDifferentSpotType() throws Exception {
        Method method = DotnetShapeHandler.class.getMethod(
            "joined",
            TestSpot.class,
            TestActor.class,
            ZLinkSpotActorChangeResult.class,
            CancellationToken.class);
        SpotActorLifecycleHandlerRegistration registration =
            new SpotActorLifecycleHandlerRegistration(
                DotnetShapeHandler.class,
                method,
                TestSpot.class,
                TestActor.class,
                ZLinkScannedHandlerKind.ACTOR_JOINED);

        assertTrue(ZLinkSpotRuntime.matchesActorLifecycleTarget(
            registration,
            new TestSpot(),
            new TestActor()));
        assertFalse(ZLinkSpotRuntime.matchesActorLifecycleTarget(
            registration,
            new OtherSpot(),
            new TestActor()));
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
        public CompletionStage<Reply> request(
            TestSpot spot,
            TestActor actor,
            ZLinkSpotActorRequestContext context,
            Request request,
            CancellationToken cancellationToken) {
            return CompletableFuture.completedFuture(new Reply());
        }

        public CompletionStage<Void> joined(
            TestSpot spot,
            TestActor actor,
            ZLinkSpotActorChangeResult result,
            CancellationToken cancellationToken) {
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class TestSpot implements ZLinkSpot {
        @Override
        public ZLinkSpotContext context() {
            throw new UnsupportedOperationException();
        }
    }

    public static final class OtherSpot implements ZLinkSpot {
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
