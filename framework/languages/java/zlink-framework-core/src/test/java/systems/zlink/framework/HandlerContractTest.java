package systems.zlink.framework;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.lang.annotation.Repeatable;
import java.lang.reflect.Method;
import java.util.Arrays;
import java.util.concurrent.CompletableFuture;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.channels.ZLinkRouteRequestCall;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkHandlerGroups;
import systems.zlink.framework.handlers.ZLinkPublish;
import systems.zlink.framework.handlers.ZLinkRequest;
import systems.zlink.framework.handlers.ZLinkSend;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.handlers.ZLinkSpotActorSend;
import systems.zlink.framework.handlers.ZLinkSpotRequest;
import systems.zlink.framework.handlers.ZLinkSpotSubscription;
import systems.zlink.framework.handlers.ZLinkSpotTimer;
import systems.zlink.framework.handlers.ZLinkStreamPacket;
import systems.zlink.framework.handlers.ZLinkStreamRaw;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorJoinEntrySpotCall;
import systems.zlink.framework.actors.ZLinkActorJoinSpotCall;
import systems.zlink.framework.actors.ZLinkBoundSessionSendCall;
import systems.zlink.framework.execution.ZLinkFrameworkTurns;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spots.ZLinkSpotHandlerRegistry;
import systems.zlink.framework.spots.ZLinkEntrySpotActorRequestHandler;
import systems.zlink.framework.spots.ZLinkEntrySpotActorSendHandler;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotActorRequestHandler;
import systems.zlink.framework.spots.ZLinkSpotActorSendHandler;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;
import systems.zlink.framework.spots.ZLinkSpotPacketHandler;
import systems.zlink.framework.spots.ZLinkSpotRequestHandler;
import systems.zlink.framework.spots.ZLinkSpotSubscriptionHandler;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionActor;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkSessionPacketDispatcher;
import systems.zlink.framework.streams.ZLinkSessionPacketHandler;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;

final class HandlerContractTest {
    @Test
    void packetAnnotationsUseDocumentedAttributeNames() {
        assertAnnotationMethods(ZLinkHandlerGroup.class, "value");
        assertAnnotationMethods(ZLinkHandlerGroups.class, "value");
        assertAnnotationMethods(ZLinkRequest.class, "packetName");
        assertAnnotationMethods(ZLinkSend.class, "packetName");
        assertAnnotationMethods(ZLinkPublish.class, "packetName");
        assertAnnotationMethods(ZLinkSpotRequest.class, "packetName");
        assertAnnotationMethods(ZLinkSpotActorRequest.class, "packetName");
        assertAnnotationMethods(ZLinkSpotActorSend.class, "packetName");
        assertAnnotationMethods(ZLinkSpotSubscription.class, "spotNodeName", "topic");
        assertAnnotationMethods(ZLinkSpotTimer.class, "name", "periodMillis");
        assertAnnotationMethods(ZLinkStreamPacket.class);
        assertAnnotationMethods(ZLinkStreamRaw.class);
    }

    @Test
    void handlerGroupAnnotationIsRepeatable() {
        Repeatable repeatable = ZLinkHandlerGroup.class.getAnnotation(Repeatable.class);

        assertSame(ZLinkHandlerGroups.class, repeatable.value());
    }

    @Test
    void handlerFilterUsesInvocationContextAndTypedNext() throws NoSuchMethodException {
        Method method = ZLinkHandlerFilter.class.getMethod(
            "invokeAsync",
            ZLinkInvocationContext.class,
            ZLinkNext.class);

        assertEquals(1, method.getTypeParameters().length);
    }

    @Test
    void routeRequestCallDoesNotExposeYieldTerminator() {
        assertFalse(hasMethod(ZLinkRouteRequestCall.class, "yield"));
    }

    @Test
    void spotHandlerRegistryMatchesDotnetRegistrationSurface() throws NoSuchMethodException {
        ZLinkSpotHandlerRegistry.class.getMethod("addHandler", Class.class);
        ZLinkSpotHandlerRegistry.class.getMethod("addPacket", Class.class);
        ZLinkSpotHandlerRegistry.class.getMethod("addSubscribe", String.class, Class.class);
        ZLinkSpotHandlerRegistry.class.getMethod("addActorPacket", Class.class);
        ZLinkSpotHandlerRegistry.class.getMethod("addActorSend", Class.class);
        ZLinkSpotHandlerRegistry.class.getMethod("addActorRequest", Class.class);
        assertFalse(hasMethod(ZLinkSpotHandlerRegistry.class, "addActor" + "Join"));
        assertFalse(hasMethod(ZLinkSpotHandlerRegistry.class, "addPostActor" + "Joined"));
        assertFalse(hasMethod(ZLinkSpotHandlerRegistry.class, "addActor" + "Left"));
        assertFalse(hasMethod(ZLinkSpotHandlerRegistry.class, "addActor" + "Disconnected"));

        ZLinkSpotPacketHandler.class.getMethod("handle", Object.class, Object.class);
        ZLinkSpotRequestHandler.class.getMethod("handle", Object.class, Object.class);
        ZLinkSpotSubscriptionHandler.class.getMethod("handle", Object.class, Object.class);
        ZLinkEntrySpotActorSendHandler.class.getMethod(
            "handle",
            ZLinkEntrySpot.class,
            systems.zlink.framework.actors.ZLinkActor.class,
            systems.zlink.framework.spots.ZLinkSpotActorSendContext.class,
            Object.class,
            CancellationToken.class);
        ZLinkEntrySpotActorRequestHandler.class.getMethod(
            "handle",
            ZLinkEntrySpot.class,
            systems.zlink.framework.actors.ZLinkActor.class,
            systems.zlink.framework.spots.ZLinkSpotActorRequestContext.class,
            Object.class,
            CancellationToken.class);
        ZLinkSpotActorSendHandler.class.getMethod(
            "handle",
            ZLinkSpot.class,
            systems.zlink.framework.actors.ZLinkActor.class,
            systems.zlink.framework.spots.ZLinkSpotActorSendContext.class,
            Object.class,
            CancellationToken.class);
        ZLinkSpotActorRequestHandler.class.getMethod(
            "handle",
            ZLinkSpot.class,
            systems.zlink.framework.actors.ZLinkActor.class,
            systems.zlink.framework.spots.ZLinkSpotActorRequestContext.class,
            Object.class,
            CancellationToken.class);
    }

    @Test
    void spotLifecycleCallbacksAreMemberContracts() throws NoSuchMethodException {
        ZLinkSpot.class.getMethod("onCreate", ZLinkMessage.class);
        ZLinkSpot.class.getMethod(
            "onActorJoin",
            ZLinkActor.class,
            ZLinkMessage.class,
            CancellationToken.class);
        ZLinkSpot.class.getMethod(
            "onJoinedActor",
            ZLinkActor.class,
            CancellationToken.class);
        ZLinkSpot.class.getMethod(
            "onLeaveActor",
            ZLinkActor.class,
            CancellationToken.class);
        ZLinkSpot.class.getMethod(
            "onDisconnectActor",
            ZLinkActor.class,
            CancellationToken.class);
        ZLinkEntrySpot.class.getMethod(
            "onActorJoin",
            ZLinkActor.class,
            ZLinkMessage.class,
            CancellationToken.class);
        ZLinkEntrySpot.class.getMethod(
            "onJoinedActor",
            ZLinkActor.class,
            CancellationToken.class);
        ZLinkEntrySpot.class.getMethod(
            "onLeaveActor",
            ZLinkActor.class,
            CancellationToken.class);
        ZLinkEntrySpot.class.getMethod(
            "onDisconnectActor",
            ZLinkActor.class,
            CancellationToken.class);
        assertTrue(ZLinkSpotActorJoinResponse.accept().accepted());
    }

    @Test
    void sessionDispatchContractsUseFrameworkMessages() throws NoSuchMethodException {
        ZLinkSession.class.getMethod("onDispatch", ZLinkSessionDispatchContext.class, ZLinkMessage.class);
        ZLinkSessionPacketHandler.class.getMethod(
            "handle",
            ZLinkSessionContext.class,
            ZLinkSessionDispatchContext.class,
            ZLinkMessage.class);
        ZLinkTypedSessionPacketHandler.class.getMethod(
            "handle",
            ZLinkSessionContext.class,
            ZLinkSessionDispatchContext.class,
            ZLinkMessage.class);
        ZLinkSessionPacketDispatcher.class.getMethod(
            "tryHandleAsync",
            ZLinkSessionContext.class,
            ZLinkSessionDispatchContext.class,
            ZLinkMessage.class);
        ZLinkSessionActor.class.getMethod("relay", ZLinkMessage.class);
    }

    @Test
    void actorJoinContractsSupportDtoAndNoReplyJoins() throws NoSuchMethodException {
        ZLinkActorContext.class.getMethod("joinSpot", RoutingId.class);
        ZLinkActorContext.class.getMethod("joinSpot", RoutingId.class, Object.class);
        ZLinkActorContext.class.getMethod("joinEntrySpot", RoutingId.class);
        ZLinkActorContext.class.getMethod("joinEntrySpot", RoutingId.class, Object.class);
        ZLinkActorJoinSpotCall.class.getMethod("submit");
        ZLinkActorJoinSpotCall.class.getMethod("await");
        ZLinkActorJoinSpotCall.class.getMethod("yield");
        ZLinkActorJoinSpotCall.class.getMethod("submit", Class.class);
        ZLinkActorJoinSpotCall.class.getMethod("await", Class.class);
        ZLinkActorJoinSpotCall.class.getMethod("yield", Class.class);
        ZLinkActorJoinEntrySpotCall.class.getMethod("submit");
        ZLinkActorJoinEntrySpotCall.class.getMethod("await");
        ZLinkActorJoinEntrySpotCall.class.getMethod("yield");
        ZLinkActorJoinEntrySpotCall.class.getMethod("submit", Class.class);
        ZLinkActorJoinEntrySpotCall.class.getMethod("await", Class.class);
        ZLinkActorJoinEntrySpotCall.class.getMethod("yield", Class.class);
    }

    @Test
    void frameworkTurnBridgeRejectsUserCreatedCompletionStage() {
        assertThrows(
            IllegalStateException.class,
            () -> ZLinkFrameworkTurns.captureCurrent());

        assertThrows(
            IllegalStateException.class,
            () -> ZLinkFrameworkTurns.awaitManagedCompletion(
                null,
                CompletableFuture.completedFuture("user-stage")));
    }

    @Test
    void oldSpotActorLifecyclePublicContractsAreRemoved() {
        assertClassMissing("systems.zlink.framework.handlers.ZLinkSpotActor" + "Join");
        assertClassMissing("systems.zlink.framework.handlers.ZLinkSpotPostActor" + "Joined");
        assertClassMissing("systems.zlink.framework.handlers.ZLinkSpotActor" + "Left");
        assertClassMissing("systems.zlink.framework.spots.ZLinkSpotActor" + "JoinHandler");
        assertClassMissing("systems.zlink.framework.spots.ZLinkSpotPostActor" + "JoinedHandler");
        assertClassMissing("systems.zlink.framework.spots.ZLinkSpotActor" + "LeftHandler");
        assertClassMissing("systems.zlink.framework.handlers.ZLinkSpotActor" + "Disconnected");
        assertClassMissing("systems.zlink.framework.spots.ZLinkSpotActor" + "DisconnectedHandler");
        assertClassMissing("systems.zlink.framework.spots.ZLinkEntrySpotActor" + "DisconnectedHandler");
        assertClassMissing("systems.zlink.framework.spots.ZLinkSpotActorChange" + "Result");
        assertClassMissing("systems.zlink.framework.spots.ZLinkSpotActorChange" + "Kind");
    }

    private static void assertAnnotationMethods(Class<?> annotationType, String... expectedNames) {
        String[] actualNames = Arrays.stream(annotationType.getDeclaredMethods())
            .map(Method::getName)
            .sorted()
            .toArray(String[]::new);
        Arrays.sort(expectedNames);

        assertTrue(
            Arrays.equals(expectedNames, actualNames),
            () -> annotationType.getSimpleName()
                + " methods were "
                + Arrays.toString(actualNames));
    }

    private static boolean hasMethod(Class<?> type, String name) {
        return Arrays.stream(type.getMethods())
            .anyMatch(method -> method.getName().equals(name));
    }

    private static void assertClassMissing(String className) {
        try {
            Class.forName(className);
        } catch (ClassNotFoundException expected) {
            return;
        }
        throw new AssertionError("class should be removed: " + className);
    }
}
