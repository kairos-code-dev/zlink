package systems.zlink.framework;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.lang.reflect.Method;
import java.util.Arrays;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.handlers.ZLinkPublish;
import systems.zlink.framework.handlers.ZLinkRequest;
import systems.zlink.framework.handlers.ZLinkSend;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.handlers.ZLinkSpotActorSend;
import systems.zlink.framework.handlers.ZLinkSpotRequest;
import systems.zlink.framework.handlers.ZLinkSpotSubscription;
import systems.zlink.framework.handlers.ZLinkStreamPacket;

final class HandlerContractTest {
    @Test
    void packetAnnotationsUseDocumentedAttributeNames() {
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
    void handlerFilterUsesInvocationContextAndTypedNext() throws NoSuchMethodException {
        Method method = ZLinkHandlerFilter.class.getMethod(
            "invokeAsync",
            ZLinkInvocationContext.class,
            ZLinkNext.class);

        assertEquals(1, method.getTypeParameters().length);
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
