package systems.zlink.framework.runtime.spots;

import java.lang.reflect.Method;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.ZLinkHandlerContext;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.runtime.internal.handlers.ZLinkHandlerActivator;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerMethodInvoker;
import systems.zlink.framework.runtime.internal.handlers.ZLinkSuspendInvocationAdapter;
import systems.zlink.framework.runtime.messaging.ZLinkMessagePayloads;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.framework.spots.ZLinkSpotActorSendContext;
import systems.zlink.framework.channels.ZLinkPublishContext;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkSendContext;

final class ZLinkSpotHandlerInvoker {
    private final ZLinkMessageSerializer serializer;
    private final ZLinkHandlerActivator handlerFactory;
    private final List<ZLinkSuspendInvocationAdapter> suspendHandlerInvokers;

    ZLinkSpotHandlerInvoker(
        ZLinkMessageSerializer serializer,
        ZLinkHandlerActivator handlerFactory,
        List<ZLinkSuspendInvocationAdapter> suspendHandlerInvokers) {
        this.serializer = serializer;
        this.handlerFactory = handlerFactory;
        this.suspendHandlerInvokers = suspendHandlerInvokers;
    }

    CompletionStage<Void> invokeActorSend(
        SpotActorPacketHandlerRegistration registration,
        Object spotSurface,
        ZLinkActor actor,
        Message payload,
        Map<String, String> metadata,
        String failureMessage) {
        Object message = deserialize(payload, registration.messageType());
        ZLinkSpotActorSendContext context =
            new ZLinkSpotActorSendHandlerContext(registration.packetName(), metadata);
        if (registration.handlerMethod() == null) {
            return invokeActorSendInterface(
                registration,
                spotSurface,
                actor,
                context,
                message,
                failureMessage);
        }
        return invokeVoidMethod(
            registration.handlerType(),
            registration.handlerMethod(),
            actorPacketArguments(
                registration.handlerMethod(),
                spotSurface,
                actor,
                context,
                message),
            failureMessage);
    }

    CompletionStage<Optional<Message>> invokeActorRequest(
        SpotActorPacketHandlerRegistration registration,
        Object spotSurface,
        ZLinkActor actor,
        Message payload,
        Map<String, String> metadata,
        String failureMessage) {
        Object message = deserialize(payload, registration.messageType());
        ZLinkSpotActorRequestContext context =
            new ZLinkSpotActorRequestHandlerContext(registration.packetName(), metadata);
        CompletionStage<Object> reply = registration.handlerMethod() == null
            ? invokeActorRequestInterface(
                registration,
                spotSurface,
                actor,
                context,
                message,
                failureMessage)
            : invokeReplyMethod(
                registration.handlerType(),
                registration.handlerMethod(),
                actorPacketArguments(
                    registration.handlerMethod(),
                    spotSurface,
                    actor,
                    context,
                    message),
                failureMessage);
        return reply.thenApply(value ->
            Optional.of(ZLinkMessagePayloads.message(serializer.serialize(value))));
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    CompletionStage<Void> invokePacket(
        SpotPacketHandlerRegistration registration,
        Object spot,
        Message payload) {
        return invokePacket(registration, spot, payload, Map.of());
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    CompletionStage<Void> invokePacket(
        SpotPacketHandlerRegistration registration,
        Object spot,
        Message payload,
        Map<String, String> metadata) {
        Object message = deserialize(payload, registration.messageType());
        ZLinkSendContext context = new ZLinkSpotSendHandlerContext(
            registration.packetName(), null, metadata);
        try {
            Object handler = handlerFactory.create(registration.handlerType());
            CompletionStage<?> stage = registration.handlerMethod() != null
                ? ZLinkHandlerMethodInvoker.invoke(
                    handler,
                    registration.handlerMethod(),
                    spotMessageArguments(
                        registration.handlerMethod(), spot, message, context),
                    suspendHandlerInvokers)
                : ZLinkHandlerMethodInvoker.invokeHandler(
                    handler,
                    "handle",
                    new Object[] {spot, message, context},
                    suspendHandlerInvokers);
            return stage.thenApply(ignored -> null);
        } catch (RuntimeException ex) {
            return failed(
                "failed to invoke SPOT packet handler: "
                    + registration.handlerType().getName(),
                ex);
        }
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    CompletionStage<Message> invokeRequest(
        SpotPacketHandlerRegistration registration,
        Object spot,
        Message payload) {
        return invokeRequest(registration, spot, payload, Map.of());
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    CompletionStage<Message> invokeRequest(
        SpotPacketHandlerRegistration registration,
        Object spot,
        Message payload,
        Map<String, String> metadata) {
        Object message = deserialize(payload, registration.messageType());
        ZLinkRequestContext context = new ZLinkSpotRequestHandlerContext(
            registration.packetName(), null, metadata);
        try {
            Object handler = handlerFactory.create(registration.handlerType());
            CompletionStage<?> stage = registration.handlerMethod() != null
                ? ZLinkHandlerMethodInvoker.invoke(
                    handler,
                    registration.handlerMethod(),
                    spotMessageArguments(
                        registration.handlerMethod(), spot, message, context),
                    suspendHandlerInvokers)
                : ZLinkHandlerMethodInvoker.invokeHandler(
                    handler,
                    "handle",
                    new Object[] {spot, message, context},
                    suspendHandlerInvokers);
            return stage.thenApply(reply ->
                ZLinkMessagePayloads.message(serializer.serialize(reply)));
        } catch (RuntimeException ex) {
            return failed(
                "failed to invoke SPOT request handler: "
                    + registration.handlerType().getName(),
                ex);
        }
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    CompletionStage<Void> invokeSubscription(
        SpotSubscriptionHandlerRegistration registration,
        Object spot,
        Message payload) {
        return invokeSubscription(
            registration,
            spot,
            null,
            null,
            Optional.empty(),
            payload,
            Map.of());
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    CompletionStage<Void> invokeSubscription(
        SpotSubscriptionHandlerRegistration registration,
        Object spot,
        String channelName,
        String topic,
        Optional<String> source,
        Message payload,
        Map<String, String> metadata) {
        Object message = deserialize(payload, registration.messageType());
        ZLinkPublishContext context = new ZLinkSpotPublishHandlerContext(
            channelName,
            registration.packetName(),
            topic,
            null,
            source,
            metadata);
        try {
            Object handler = handlerFactory.create(registration.handlerType());
            CompletionStage<?> stage = registration.handlerMethod() != null
                ? ZLinkHandlerMethodInvoker.invoke(
                    handler,
                    registration.handlerMethod(),
                    spotMessageArguments(
                        registration.handlerMethod(), spot, message, context),
                    suspendHandlerInvokers)
                : ZLinkHandlerMethodInvoker.invokeHandler(
                    handler,
                    "handle",
                    new Object[] {spot, message, context},
                    suspendHandlerInvokers);
            return stage.thenApply(ignored -> null);
        } catch (RuntimeException ex) {
            return failed(
                "failed to invoke SPOT subscription handler: "
                    + registration.handlerType().getName(),
                ex);
        }
    }

    static Object[] actorPacketArguments(
        Method method,
        Object spot,
        ZLinkActor actor,
        ZLinkHandlerContext context,
        Object message) {
        Class<?>[] parameterTypes = ZLinkHandlerMethodInvoker.logicalParameterTypes(method);
        if (parameterTypes.length == 2) {
            return new Object[] {actor, message};
        }
        return new Object[] {spot, actor, context, message};
    }

    private static Object[] spotMessageArguments(
        Method method,
        Object spot,
        Object message,
        ZLinkHandlerContext context) {
        Class<?>[] parameterTypes =
            ZLinkHandlerMethodInvoker.logicalParameterTypes(method);
        if (parameterTypes.length == 2) {
            return new Object[] {spot, message};
        }
        Object[] arguments = new Object[parameterTypes.length];
        for (int index = 0; index < parameterTypes.length; index++) {
            if (parameterTypes[index].isInstance(context)) {
                arguments[index] = context;
            } else if (parameterTypes[index].isInstance(spot)) {
                arguments[index] = spot;
            } else {
                arguments[index] = message;
            }
        }
        return arguments;
    }

    private CompletionStage<Void> invokeVoidMethod(
        Class<?> handlerType,
        Method method,
        Object[] arguments,
        String failureMessage) {
        return invokeReplyMethod(handlerType, method, arguments, failureMessage)
            .thenApply(ignored -> null);
    }

    private CompletionStage<Object> invokeReplyMethod(
        Class<?> handlerType,
        Method method,
        Object[] arguments,
        String failureMessage) {
        try {
            Object handler = handlerFactory.create(handlerType);
            return ZLinkHandlerMethodInvoker.invoke(
                handler,
                method,
                arguments,
                suspendHandlerInvokers);
        } catch (RuntimeException ex) {
            return failed(
                failureMessage + ": " + handlerType.getName() + "." + method.getName(),
                ex);
        }
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    private CompletionStage<Void> invokeActorSendInterface(
        SpotActorPacketHandlerRegistration registration,
        Object spotSurface,
        ZLinkActor actor,
        ZLinkSpotActorSendContext context,
        Object message,
        String failureMessage) {
        try {
            Object handler = handlerFactory.create(registration.handlerType());
            return ZLinkHandlerMethodInvoker
                .invokeHandler(
                    handler,
                    "handle",
                    new Object[] {spotSurface, actor, context, message},
                    suspendHandlerInvokers)
                .thenApply(ignored -> null);
        } catch (RuntimeException ex) {
            return failed(
                failureMessage + ": " + registration.handlerType().getName(),
                ex);
        }
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    private CompletionStage<Object> invokeActorRequestInterface(
        SpotActorPacketHandlerRegistration registration,
        Object spotSurface,
        ZLinkActor actor,
        ZLinkSpotActorRequestContext context,
        Object message,
        String failureMessage) {
        try {
            Object handler = handlerFactory.create(registration.handlerType());
            return ZLinkHandlerMethodInvoker.invokeHandler(
                handler,
                "handle",
                new Object[] {spotSurface, actor, context, message},
                suspendHandlerInvokers);
        } catch (RuntimeException ex) {
            return failed(
                failureMessage + ": " + registration.handlerType().getName(),
                ex);
        }
    }

    private Object deserialize(Message payload, Class<?> messageType) {
        return ZLinkMessagePayloads.deserialize(serializer, payload, messageType);
    }

    private static <T> CompletionStage<T> failed(String message, RuntimeException error) {
        return CompletableFuture.failedFuture(new ZLinkConfigurationException(message, error));
    }

}
