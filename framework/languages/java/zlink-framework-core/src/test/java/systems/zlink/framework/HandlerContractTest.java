package systems.zlink.framework;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.lang.annotation.Repeatable;
import java.lang.reflect.Method;
import java.util.Arrays;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkHandlerGroups;
import systems.zlink.framework.handlers.ZLinkPublish;
import systems.zlink.framework.handlers.ZLinkRequest;
import systems.zlink.framework.handlers.ZLinkSend;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.handlers.ZLinkSpotActorSend;
import systems.zlink.framework.handlers.ZLinkSpotRequest;
import systems.zlink.framework.handlers.ZLinkSpotSubscription;
import systems.zlink.framework.handlers.ZLinkStreamPacket;
import systems.zlink.framework.spots.ZLinkSpotHandlerRegistry;
import systems.zlink.framework.spots.ZLinkSpotPacketHandler;
import systems.zlink.framework.spots.ZLinkSpotRequestHandler;
import systems.zlink.framework.spots.ZLinkSpotSubscriptionHandler;

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
        assertAnnotationMethods(ZLinkStreamPacket.class);
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
    void spotHandlerRegistryMatchesDotnetRegistrationSurface() throws NoSuchMethodException {
        ZLinkSpotHandlerRegistry.class.getMethod("addHandler", Class.class);
        ZLinkSpotHandlerRegistry.class.getMethod("addPacket", Class.class);
        ZLinkSpotHandlerRegistry.class.getMethod("addSubscribe", String.class, Class.class);
        ZLinkSpotHandlerRegistry.class.getMethod("addActorJoin", Class.class);
        ZLinkSpotHandlerRegistry.class.getMethod("addActorPacket", Class.class);
        ZLinkSpotHandlerRegistry.class.getMethod("addPostActorJoined", Class.class);
        ZLinkSpotHandlerRegistry.class.getMethod("addActorLeft", Class.class);
        ZLinkSpotHandlerRegistry.class.getMethod("addActorDisconnected", Class.class);

        ZLinkSpotPacketHandler.class.getMethod("handleAsync", Object.class, Object.class);
        ZLinkSpotRequestHandler.class.getMethod("handleAsync", Object.class, Object.class);
        ZLinkSpotSubscriptionHandler.class.getMethod("handleAsync", Object.class, Object.class);
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
}
