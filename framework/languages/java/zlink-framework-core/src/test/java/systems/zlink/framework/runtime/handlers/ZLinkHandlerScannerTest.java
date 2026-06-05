package systems.zlink.framework.runtime.handlers;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.ZLinkHandlerContext;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkSendContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkRequest;
import systems.zlink.framework.handlers.ZLinkSend;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.framework.spots.ZLinkSpotContext;

final class ZLinkHandlerScannerTest {
    @Test
    void scansUngroupedInterfaceHandlersLikeDotnet() {
        ZLinkScannedHandlerCatalog catalog =
            ZLinkHandlerScanner.scan(Set.of(ZLinkHandlerScannerTest.class));

        ZLinkScannedHandler handler = catalog.handlers().stream()
            .filter(candidate -> candidate.handlerType() == UngroupedInterfaceHandler.class)
            .findFirst()
            .orElseThrow();

        assertEquals(ZLinkScannedHandlerSurface.CHANNEL, handler.surface());
        assertEquals(ZLinkScannedHandlerKind.REQUEST, handler.kind());
        assertEquals(String.class, handler.messageType());
        assertEquals(String.class, handler.replyType());
        assertTrue(handler.groups().isEmpty());
        assertTrue(catalog.matching(
            Set.of("missing"),
            ZLinkScannedHandlerSurface.CHANNEL,
            ZLinkScannedHandlerKind.REQUEST).stream()
            .noneMatch(candidate -> candidate.handlerType() == UngroupedInterfaceHandler.class));
    }

    @Test
    void scansUngroupedAttributedHandlersLikeDotnet() {
        ZLinkScannedHandlerCatalog catalog =
            ZLinkHandlerScanner.scan(Set.of(ZLinkHandlerScannerTest.class));

        ZLinkScannedHandler handler = catalog.handlers().stream()
            .filter(candidate -> candidate.handlerType() == UngroupedAttributedHandler.class)
            .findFirst()
            .orElseThrow();

        assertEquals(ZLinkScannedHandlerSurface.CHANNEL, handler.surface());
        assertEquals(ZLinkScannedHandlerKind.REQUEST, handler.kind());
        assertEquals("Echo", handler.packetName());
        assertTrue(handler.groups().isEmpty());
    }

    @Test
    void scansAttributedHandlersWithContextParameterLikeDotnet() {
        ZLinkScannedHandlerCatalog catalog =
            ZLinkHandlerScanner.scan(Set.of(ZLinkHandlerScannerTest.class));

        ZLinkScannedHandler requestHandler = catalog.handlers().stream()
            .filter(candidate -> candidate.handlerType() == ContextAttributedRequestHandler.class)
            .findFirst()
            .orElseThrow();
        ZLinkScannedHandler sendHandler = catalog.handlers().stream()
            .filter(candidate -> candidate.handlerType() == ContextAttributedSendHandler.class)
            .findFirst()
            .orElseThrow();

        assertEquals(ZLinkScannedHandlerKind.REQUEST, requestHandler.kind());
        assertEquals("ContextRequest", requestHandler.packetName());
        assertEquals(ZLinkScannedHandlerKind.SEND, sendHandler.kind());
        assertEquals("ContextSend", sendHandler.packetName());
    }

    @Test
    void scansAttributedHandlersWithDefaultParameterLikeDotnet() {
        ZLinkScannedHandlerCatalog catalog =
            ZLinkHandlerScanner.scan(Set.of(ZLinkHandlerScannerTest.class));

        ZLinkScannedHandler handler = catalog.handlers().stream()
            .filter(candidate -> candidate.handlerType() == DefaultParameterAttributedHandler.class)
            .findFirst()
            .orElseThrow();

        assertEquals(ZLinkScannedHandlerKind.REQUEST, handler.kind());
        assertEquals(String.class, handler.messageType());
        assertEquals("DefaultParameterRequest", handler.packetName());
    }

    @Test
    void scansRepeatableHandlerGroupsLikeDotnetAllowMultipleAttribute() {
        ZLinkScannedHandlerCatalog catalog =
            ZLinkHandlerScanner.scan(Set.of(ZLinkHandlerScannerTest.class));

        ZLinkScannedHandler handler = catalog.handlers().stream()
            .filter(candidate -> candidate.handlerType() == RepeatableGroupHandler.class)
            .findFirst()
            .orElseThrow();

        assertEquals(Set.of("primary", "secondary"), handler.groups());
    }

    @Test
    void scansSpotActorHandlerWithDotnetAttributedShape() {
        ZLinkScannedHandlerCatalog catalog =
            ZLinkHandlerScanner.scan(Set.of(ZLinkHandlerScannerTest.class));

        ZLinkScannedHandler handler = catalog.handlers().stream()
            .filter(candidate -> candidate.handlerType() == DotnetShapeSpotActorHandler.class)
            .findFirst()
            .orElseThrow();

        assertEquals(ZLinkScannedHandlerSurface.SPOT, handler.surface());
        assertEquals(ZLinkScannedHandlerKind.ACTOR_REQUEST, handler.kind());
        assertEquals(SpotActorRequest.class, handler.messageType());
        assertEquals(SpotActorReply.class, handler.replyType());
        assertEquals("SpotActorRequest", handler.packetName());
    }

    public static final class UngroupedInterfaceHandler
        implements ZLinkRequestHandler<String, String> {
        @Override
        public CompletionStage<String> handleAsync(
            String request,
            ZLinkRequestContext context) {
            return CompletableFuture.completedFuture(request);
        }
    }

    public static final class UngroupedAttributedHandler {
        @ZLinkRequest(packetName = "Echo")
        public CompletionStage<String> handle(String request) {
            return CompletableFuture.completedFuture(request);
        }
    }

    public static final class ContextAttributedRequestHandler {
        @ZLinkRequest(packetName = "ContextRequest")
        public CompletionStage<String> handle(String request, ZLinkRequestContext context) {
            return CompletableFuture.completedFuture(context.packetName().orElse(request));
        }
    }

    public static final class ContextAttributedSendHandler {
        @ZLinkSend(packetName = "ContextSend")
        public CompletionStage<Void> handle(String request, ZLinkHandlerContext context) {
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class DefaultParameterAttributedHandler {
        @ZLinkRequest(packetName = "DefaultParameterRequest")
        public CompletionStage<String> handle(
            String request,
            Object defaultParameter,
            ZLinkRequestContext context,
            CancellationToken cancellationToken) {
            return CompletableFuture.completedFuture(request);
        }
    }

    @ZLinkHandlerGroup("primary")
    @ZLinkHandlerGroup("secondary")
    public static final class RepeatableGroupHandler {
        @ZLinkSend(packetName = "RepeatableGroupSend")
        public CompletionStage<Void> handle(String request) {
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class DotnetShapeSpotActorHandler {
        @ZLinkSpotActorRequest(packetName = "SpotActorRequest")
        public CompletionStage<SpotActorReply> handle(
            TestSpot spot,
            TestActor actor,
            ZLinkSpotActorRequestContext context,
            SpotActorRequest request,
            CancellationToken cancellationToken) {
            return CompletableFuture.completedFuture(new SpotActorReply());
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

    public record SpotActorRequest() {
    }

    public record SpotActorReply() {
    }
}
