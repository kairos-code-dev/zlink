package systems.zlink.framework.runtime.handlers;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.Set;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.ZLinkHandlerContext;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.channels.ZLinkPublishContext;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkSendContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkPacket;
import systems.zlink.framework.handlers.ZLinkPublish;
import systems.zlink.framework.handlers.ZLinkRequest;
import systems.zlink.framework.handlers.ZLinkSend;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.handlers.ZLinkSpotRequest;
import systems.zlink.framework.handlers.ZLinkSpotSubscription;
import systems.zlink.framework.handlers.ZLinkSpotTimer;
import systems.zlink.testfixtures.handlerconflict.ConflictingSpotActorPacketHandler;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.spots.ZLinkEntrySpotActorRequestHandler;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotPacketHandler;
import systems.zlink.framework.spots.ZLinkSpotSubscriptionHandler;
import systems.zlink.framework.spots.ZLinkSpotTimerHandler;
import systems.zlink.framework.spots.ZLinkTimerTick;
import systems.zlink.testfixtures.handlerasync.CompletionStageAttributedHandler;

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
    void scansAttributedPublishHandlerWithContextParameterLikeDotnet() {
        ZLinkScannedHandlerCatalog catalog =
            ZLinkHandlerScanner.scan(Set.of(ZLinkHandlerScannerTest.class));

        ZLinkScannedHandler handler = catalog.handlers().stream()
            .filter(candidate -> candidate.handlerType() == ContextAttributedPublishHandler.class)
            .findFirst()
            .orElseThrow();

        assertEquals(ZLinkScannedHandlerSurface.CHANNEL, handler.surface());
        assertEquals(ZLinkScannedHandlerKind.PUBLISH, handler.kind());
        assertEquals(String.class, handler.messageType());
        assertEquals(Void.class, handler.replyType());
        assertEquals("ContextPublish", handler.packetName());
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
        assertEquals(TestSpot.class, handler.spotType());
        assertEquals(SpotActorRequest.class, handler.messageType());
        assertEquals(SpotActorReply.class, handler.replyType());
        assertEquals("SpotActorRequest", handler.packetName());
    }

    @Test
    void scansSpotActorInterfaceHandlersLikeDotnet() {
        ZLinkScannedHandlerCatalog catalog =
            ZLinkHandlerScanner.scan(Set.of(ZLinkHandlerScannerTest.class));

        ZLinkScannedHandler request = catalog.handlers().stream()
            .filter(candidate -> candidate.handlerType() == InterfaceEntrySpotActorRequestHandler.class)
            .findFirst()
            .orElseThrow();

        assertEquals(ZLinkScannedHandlerSurface.SPOT, request.surface());
        assertEquals(ZLinkScannedHandlerKind.ACTOR_REQUEST, request.kind());
        assertEquals(TestEntrySpot.class, request.spotType());
        assertEquals(SpotActorRequest.class, request.messageType());
        assertEquals(SpotActorReply.class, request.replyType());
        assertEquals("InterfaceSpotActorRequest", request.packetName());
        assertTrue(catalog.handlers().stream()
            .noneMatch(candidate -> candidate.kind() == ZLinkScannedHandlerKind.ACTOR_JOIN
                || candidate.kind() == ZLinkScannedHandlerKind.ACTOR_JOINED
                || candidate.kind() == ZLinkScannedHandlerKind.ACTOR_LEFT));
    }

    @Test
    void scansSpotPacketSubscriptionAndTimerHandlersLikeDotnet() {
        ZLinkScannedHandlerCatalog catalog =
            ZLinkHandlerScanner.scan(Set.of(ZLinkHandlerScannerTest.class));

        ZLinkScannedHandler packet = catalog.handlers().stream()
            .filter(candidate -> candidate.handlerType() == InterfaceSpotPacketHandler.class)
            .findFirst()
            .orElseThrow();
        ZLinkScannedHandler request = catalog.handlers().stream()
            .filter(candidate -> candidate.handlerType() == AttributedSpotRequestHandler.class)
            .findFirst()
            .orElseThrow();
        ZLinkScannedHandler subscription = catalog.handlers().stream()
            .filter(candidate -> candidate.handlerType() == InterfaceSpotSubscriptionHandler.class)
            .findFirst()
            .orElseThrow();
        ZLinkScannedHandler timer = catalog.handlers().stream()
            .filter(candidate -> candidate.handlerType() == InterfaceSpotTimerHandler.class)
            .findFirst()
            .orElseThrow();

        assertEquals(ZLinkScannedHandlerSurface.SPOT, packet.surface());
        assertEquals(ZLinkScannedHandlerKind.SEND, packet.kind());
        assertEquals(TestSpot.class, packet.spotType());
        assertEquals(SpotPacket.class, packet.messageType());
        assertEquals("SpotPacket", packet.packetName());

        assertEquals(ZLinkScannedHandlerKind.REQUEST, request.kind());
        assertEquals(TestSpot.class, request.spotType());
        assertEquals(SpotRequest.class, request.messageType());
        assertEquals(SpotReply.class, request.replyType());
        assertEquals("SpotRequestPacket", request.packetName());

        assertEquals(ZLinkScannedHandlerKind.PUBLISH, subscription.kind());
        assertEquals(TestSpot.class, subscription.spotType());
        assertEquals(SpotEvent.class, subscription.messageType());
        assertEquals("room.events", subscription.topic());

        assertEquals(ZLinkScannedHandlerKind.TIMER, timer.kind());
        assertEquals(TestSpot.class, timer.spotType());
        assertEquals("heartbeat", timer.timerName());
        assertEquals(java.time.Duration.ofMillis(250), timer.timerPeriod());
    }

    @Test
    void rejectsConflictingSpotActorPacketAnnotationsLikeDotnet() {
        assertThrows(
            ZLinkConfigurationException.class,
            () -> ZLinkHandlerScanner.scan(Set.of(ConflictingSpotActorPacketHandler.class)));
    }

    @Test
    void rejectsJavaCompletionStageAttributedHandlerReturn() {
        assertThrows(
            ZLinkConfigurationException.class,
            () -> ZLinkHandlerScanner.scan(Set.of(CompletionStageAttributedHandler.class)));
    }

    public static final class UngroupedInterfaceHandler
        implements ZLinkRequestHandler<String, String> {
        @Override
        public String handle(
            String request,
            ZLinkRequestContext context) {
            return request;
        }
    }

    public static final class UngroupedAttributedHandler {
        @ZLinkRequest(packetName = "Echo")
        public String handle(String request) {
            return request;
        }
    }

    public static final class ContextAttributedRequestHandler {
        @ZLinkRequest(packetName = "ContextRequest")
        public String handle(String request, ZLinkRequestContext context) {
            return context.packetName().orElse(request);
        }
    }

    public static final class ContextAttributedSendHandler {
        @ZLinkSend(packetName = "ContextSend")
        public void handle(String request, ZLinkHandlerContext context) {
        }
    }

    public static final class ContextAttributedPublishHandler {
        @ZLinkPublish(packetName = "ContextPublish")
        public void handle(String request, ZLinkPublishContext context) {
        }
    }

    public static final class DefaultParameterAttributedHandler {
        @ZLinkRequest(packetName = "DefaultParameterRequest")
        public String handle(
            String request,
            Object defaultParameter,
            ZLinkRequestContext context,
            CancellationToken cancellationToken) {
            return request;
        }
    }

    @ZLinkHandlerGroup("primary")
    @ZLinkHandlerGroup("secondary")
    public static final class RepeatableGroupHandler {
        @ZLinkSend(packetName = "RepeatableGroupSend")
        public void handle(String request) {
        }
    }

    public static final class DotnetShapeSpotActorHandler {
        @ZLinkSpotActorRequest(packetName = "SpotActorRequest")
        public SpotActorReply handle(
            TestSpot spot,
            TestActor actor,
            ZLinkSpotActorRequestContext context,
            SpotActorRequest request,
            CancellationToken cancellationToken) {
            return new SpotActorReply();
        }
    }

    public static final class TestSpot implements ZLinkSpot {
        @Override
        public ZLinkSpotContext context() {
            throw new UnsupportedOperationException();
        }
    }

    public static final class TestEntrySpot implements ZLinkEntrySpot {
        @Override
        public ZLinkEntrySpotContext context() {
            throw new UnsupportedOperationException();
        }
    }

    public static final class InterfaceEntrySpotActorRequestHandler
        implements ZLinkEntrySpotActorRequestHandler<
            TestEntrySpot,
            TestActor,
            SpotActorRequest,
            SpotActorReply> {
        @Override
        public SpotActorReply handle(
            TestEntrySpot entrySpot,
            TestActor actor,
            ZLinkSpotActorRequestContext context,
            SpotActorRequest request,
            CancellationToken cancellationToken) {
            return new SpotActorReply();
        }
    }

    public static final class InterfaceSpotPacketHandler
        implements ZLinkSpotPacketHandler<TestSpot, SpotPacket> {
        @Override
        public void handle(TestSpot spot, SpotPacket message) {
        }
    }

    public static final class AttributedSpotRequestHandler {
        @ZLinkSpotRequest(packetName = "SpotRequestPacket")
        public SpotReply handle(TestSpot spot, SpotRequest request) {
            return new SpotReply();
        }
    }

    @ZLinkSpotSubscription(topic = "room.events")
    public static final class InterfaceSpotSubscriptionHandler
        implements ZLinkSpotSubscriptionHandler<TestSpot, SpotEvent> {
        @Override
        public void handle(TestSpot spot, SpotEvent message) {
        }
    }

    @ZLinkSpotTimer(name = "heartbeat", periodMillis = 250)
    public static final class InterfaceSpotTimerHandler
        implements ZLinkSpotTimerHandler<TestSpot> {
        @Override
        public void handle(TestSpot spot, ZLinkTimerTick tick) {
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

    @ZLinkPacket("InterfaceSpotActorRequest")
    public record SpotActorRequest() {
    }

    public record SpotActorReply() {
    }

    public record SpotPacket() {
    }

    public record SpotRequest() {
    }

    public record SpotReply() {
    }

    public record SpotEvent() {
    }
}
