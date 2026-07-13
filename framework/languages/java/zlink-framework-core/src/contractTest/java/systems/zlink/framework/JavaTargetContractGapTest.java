package systems.zlink.framework;

import systems.zlink.framework.runtime.internal.handlers.ZLinkSuspendInvocationAdapter;

import systems.zlink.framework.runtime.internal.handlers.ZLinkHandlerActivator;

import systems.zlink.framework.runtime.internal.backend.ZLinkBackendAdapterProvider;

import systems.zlink.framework.runtime.internal.backend.ZLinkInternalSpotNode;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assertions.fail;

import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.Arrays;
import java.util.Optional;
import java.util.concurrent.CompletionStage;
import org.junit.jupiter.api.Test;

final class JavaTargetContractGapTest {
    @Test
    void handlersFactoriesAndLifecycleExposeCompletionStages() throws Exception {
        assertNamedMethodsReturnStage("systems.zlink.framework.channels.ZLinkRequestHandler", "handle");
        assertNamedMethodsReturnStage("systems.zlink.framework.channels.ZLinkSendHandler", "handle");
        assertNamedMethodsReturnStage("systems.zlink.framework.channels.ZLinkPublishHandler", "handle");
        assertNamedMethodsReturnStage("systems.zlink.framework.actors.ZLinkActorFactory", "create");
    }

    @Test
    void oneWayCallsDoNotExportCompletionOrLegacyTerminators() throws Exception {
        assertClassAbsent("systems.zlink.framework.ZLinkSubmitStage");
        assertClassAbsent("systems.zlink.framework.ZLinkAwait");
        assertClassAbsent("systems.zlink.framework.channels.ZLinkYieldRequestCall");
        assertPublicMethodReturnsVoid("systems.zlink.framework.channels.ZLinkSendCall", "submit");
        assertPublicMethodReturnsVoid("systems.zlink.framework.actors.ZLinkActorSendCall", "submit");
        assertNoPublicMethodNamed("systems.zlink.framework.channels.ZLinkSendCall", "await");
        assertNoPublicMethodNamed("systems.zlink.framework.channels.ZLinkRequestCall", "await");
        assertNoPublicMethodNamed("systems.zlink.framework.actors.ZLinkActorSendCall", "await");
        assertNoPublicMethodNamed("systems.zlink.framework.actors.ZLinkActorRequestCall", "await");
        assertNoPublicMethodNamed("systems.zlink.framework.spots.ZLinkWorkerCall", "yield");
        assertClassAbsent("systems.zlink.framework.actors.ZLinkActorJoinSpotCall");
    }

    @Test
    void javaContractDoesNotExportFrameworkCancellationToken() {
        assertClassAbsent("systems.zlink.framework.CancellationToken");
    }

    @Test
    void actorContextUsesOneMembershipValueAndOneJoinContract() throws Exception {
        Class<?> context = Class.forName("systems.zlink.framework.actors.ZLinkActorContext");
        assertNoPublicMethodNamed(context, "isJoined");
        assertNoPublicMethodNamed(context, "getSpot");
        Method spotRid = context.getMethod("spotRid");
        assertEquals(Optional.class, spotRid.getReturnType());
        assertNotNull(Class.forName("systems.zlink.framework.actors.ZLinkActorJoinCall"));
        Class<?> result = Class.forName("systems.zlink.framework.actors.ZLinkActorJoinResult");
        assertTrue(result.isSealed(), "actor join result must be sealed");
    }

    @Test
    void spotAddressIsHiddenBehindOpaqueHandle() throws Exception {
        assertNotNull(Class.forName("systems.zlink.framework.spots.SpotHandle"));
        assertNotNull(Class.forName("systems.zlink.framework.spots.SpotHandleResolver"));
        assertNotNull(Class.forName("systems.zlink.framework.spots.ActorSpotHandleResolver"));
        assertClassAbsent("systems.zlink.framework.spots.SpotRemoteRef");
        assertClassAbsent("systems.zlink.framework.spots.SpotRemoteRefResolver");
    }

    @Test
    void dispatchStrategyAndTypedPacketOverridesAreNotPublic() throws Exception {
        assertClassAbsent("systems.zlink.framework.configuration.ZLinkDispatchMode");
        assertNoPublicMethodNamed("systems.zlink.framework.channels.ZLinkSendCall", "packetName");
        assertNoPublicMethodNamed("systems.zlink.framework.channels.ZLinkRequestCall", "packetName");
    }

    @Test
    void spotManagerExposesRequestCreationAndContextCloseContracts() throws Exception {
        Class<?> manager = Class.forName("systems.zlink.framework.spots.ZLinkSpotManager");
        assertTrue(Arrays.stream(manager.getMethods()).anyMatch(method ->
            method.getName().equals("create") && method.getParameterCount() >= 2));
        Class<?> context = Class.forName("systems.zlink.framework.spots.ZLinkSpotContext");
        assertTrue(Arrays.stream(context.getMethods()).anyMatch(method -> method.getName().equals("close")));
    }

    @Test
    void routeMeshAndManualConnectionsUseTheTargetRuntimeContracts() throws Exception {
        Class<?> options = Class.forName("systems.zlink.framework.channels.ZLinkChannelRuntimeOptions");
        assertNotNull(options.getMethod("routeMeshChannel", String.class));
        assertNotNull(Class.forName("systems.zlink.framework.channels.ZLinkRouteMeshChannelRuntimeOptions"));
        Class<?> connections = Class.forName(
            "systems.zlink.framework.configuration.ZLinkEndpointConnections");
        assertNotNull(connections.getMethod("connect", String.class));
        assertNotNull(connections.getMethod("disconnect", String.class));
        assertNotNull(connections.getMethod("listConnections"));
        assertNotNull(Class.forName("systems.zlink.framework.configuration.ClientServerChannelBuilder")
            .getMethod("clientConnections"));
        assertNotNull(Class.forName("systems.zlink.framework.configuration.FanoutChannelBuilder")
            .getMethod("subscriberConnections"));
        assertNotNull(Class.forName("systems.zlink.framework.configuration.RouteMeshChannelBuilder")
            .getMethod("clientConnections"));
    }

    @Test
    void typedSessionHandlerDoesNotInheritRawApplicationHandler() throws Exception {
        Class<?> typed = Class.forName("systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler");
        assertClassAbsent("systems.zlink.framework.streams.ZLinkSessionPacketHandler");
        assertNamedMethodsReturnStage(typed.getName(), "handle");
    }

    @Test
    void observabilityOperationsContractIsExported() throws Exception {
        assertNotNull(Class.forName("systems.zlink.framework.monitoring.ZLinkFlowOrigin"));
        assertNotNull(Class.forName("systems.zlink.framework.monitoring.ZLinkDrainControl"));
        Class<?> result = Class.forName("systems.zlink.framework.monitoring.ZLinkDrainResult");
        assertTrue(result.isSealed(), "drain result must be sealed");
        Class<?> peer = Class.forName("systems.zlink.framework.locations.ZLinkPeerLocation");
        assertNotNull(peer.getMethod("draining"));
    }

    @Test
    void backendAndHandlerRuntimeTypesAreNotApplicationPublicSurface() throws Exception {
        assertClassAbsent("systems.zlink.framework.runtime.backend.ZLinkBackendAdapterFactory");
        assertClassAbsent("systems.zlink.framework.runtime.backend.ZLinkBackendSpotNode");
        assertClassAbsent("systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory");
        assertClassAbsent("systems.zlink.framework.runtime.handlers.ZLinkSuspendHandlerInvoker");

        assertInternalRuntimeSpi(
            "systems.zlink.framework.runtime.internal.backend.ZLinkBackendAdapterProvider");
        assertInternalRuntimeSpi(
            "systems.zlink.framework.runtime.internal.backend.ZLinkInternalSpotNode");
        assertInternalRuntimeSpi(
            "systems.zlink.framework.runtime.internal.handlers.ZLinkHandlerActivator");
        assertInternalRuntimeSpi(
            "systems.zlink.framework.runtime.internal.handlers.ZLinkSuspendInvocationAdapter");
    }

    @Test
    void completionStageOperationsDoNotUseAsyncSuffix() throws Exception {
        Class<?> store = Class.forName("systems.zlink.framework.locations.ZLinkLocationStore");
        for (Method method : store.getMethods()) {
            if (CompletionStage.class.isAssignableFrom(method.getReturnType())) {
                assertFalse(method.getName().endsWith("Async"), method.toString());
            }
        }
    }

    @Test
    void locationContractIncludesSealedKeyAndRuntimeFacets() throws Exception {
        Class<?> key = Class.forName("systems.zlink.framework.locations.ZLinkLocationKey");
        assertTrue(key.isSealed(), "location key must be sealed");
        assertNotNull(Class.forName("systems.zlink.framework.locations.ZLinkLocationReadiness"));
        assertNotNull(Class.forName("systems.zlink.framework.locations.ZLinkLocationRuntimeQuery"));
        assertNotNull(Class.forName("systems.zlink.framework.locations.ZLinkLocationWatchStore"));
    }

    @Test
    void javaCoreDoesNotReimplementKotlinSuspendInvocation() throws Exception {
        Class<?> invoker = Class.forName("systems.zlink.framework.runtime.handlers.ZLinkHandlerMethodInvoker");
        assertFalse(Arrays.stream(invoker.getDeclaredMethods()).anyMatch(method ->
            method.getName().equals("invokeKotlinSuspend")),
            "Kotlin module provider must be the only coroutine invocation owner");
    }

    private static void assertNamedMethodsReturnStage(String className, String methodName) throws Exception {
        Class<?> type = Class.forName(className);
        Method[] methods = Arrays.stream(type.getMethods())
            .filter(method -> method.getName().equals(methodName))
            .toArray(Method[]::new);
        assertTrue(methods.length > 0, className + "." + methodName);
        for (Method method : methods) {
            assertTrue(CompletionStage.class.isAssignableFrom(method.getReturnType()), method.toString());
        }
    }

    private static void assertPublicMethodReturnsVoid(String className, String methodName) throws Exception {
        Class<?> type = Class.forName(className);
        Method method = Arrays.stream(type.getMethods())
            .filter(candidate -> candidate.getName().equals(methodName))
            .findFirst()
            .orElseThrow();
        assertEquals(void.class, method.getReturnType(), method.toString());
    }

    private static void assertNoPublicMethodNamed(String className, String methodName) throws Exception {
        assertNoPublicMethodNamed(Class.forName(className), methodName);
    }

    private static void assertNoPublicMethodNamed(Class<?> type, String methodName) {
        assertFalse(Arrays.stream(type.getMethods()).anyMatch(method -> method.getName().equals(methodName)),
            type.getName() + "." + methodName);
    }

    private static void assertNotPublic(String className) throws Exception {
        Class<?> type = Class.forName(className);
        assertFalse(Modifier.isPublic(type.getModifiers()), className + " must not be public");
    }

    private static void assertInternalRuntimeSpi(String className) throws Exception {
        Class<?> type = Class.forName(className);
        assertTrue(type.getPackageName().startsWith("systems.zlink.framework.runtime.internal."));
        assertFalse(type.getPackageName().startsWith("systems.zlink.framework.configuration"));
        assertFalse(type.getPackageName().startsWith("systems.zlink.framework.channels"));
        assertFalse(type.getPackageName().startsWith("systems.zlink.framework.spots"));
        assertFalse(type.getPackageName().startsWith("systems.zlink.framework.streams"));
    }

    private static void assertClassAbsent(String className) {
        try {
            Class.forName(className);
            fail(className + " must not be exported");
        } catch (ClassNotFoundException expected) {
            // Target contract reached.
        }
    }
}
