package systems.zlink.framework.runtime.spots;

import java.lang.reflect.Method;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.ZLinkHandlerContext;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerMethodInvoker;
import systems.zlink.framework.runtime.handlers.ZLinkSuspendHandlerInvoker;
import systems.zlink.framework.runtime.messaging.ZLinkMessagePayloads;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.framework.spots.ZLinkSpotActorSendContext;

final class ZLinkSpotHandlerInvoker {
    private final ZLinkMessageSerializer serializer;
    private final ZLinkHandlerFactory handlerFactory;
    private final List<ZLinkSuspendHandlerInvoker> suspendHandlerInvokers;

    ZLinkSpotHandlerInvoker(
        ZLinkMessageSerializer serializer,
        ZLinkHandlerFactory handlerFactory,
        List<ZLinkSuspendHandlerInvoker> suspendHandlerInvokers) {
        this.serializer = serializer;
        this.handlerFactory = handlerFactory;
        this.suspendHandlerInvokers = suspendHandlerInvokers;
    }

    CompletionStage<Void> invokeActorSend(
        SpotActorPacketHandlerRegistration registration,
        Object spotSurface,
        ZLinkActor actor,
        Message payload,
        String failureMessage) {
        Object message = deserialize(payload, registration.messageType());
        ZLinkSpotActorSendContext context =
            new ZLinkSpotActorSendHandlerContext(registration.packetName());
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
        String failureMessage) {
        Object message = deserialize(payload, registration.messageType());
        ZLinkSpotActorRequestContext context =
            new ZLinkSpotActorRequestHandlerContext(registration.packetName());
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
        Object message = deserialize(payload, registration.messageType());
        try {
            Object handler = handlerFactory.create(registration.handlerType());
            CompletionStage<?> stage = registration.handlerMethod() != null
                ? ZLinkHandlerMethodInvoker.invoke(
                    handler,
                    registration.handlerMethod(),
                    new Object[] {spot, message},
                    suspendHandlerInvokers)
                : ZLinkHandlerMethodInvoker.invokeHandler(
                    handler,
                    "handle",
                    new Object[] {spot, message},
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
        Object message = deserialize(payload, registration.messageType());
        try {
            Object handler = handlerFactory.create(registration.handlerType());
            CompletionStage<?> stage = registration.handlerMethod() != null
                ? ZLinkHandlerMethodInvoker.invoke(
                    handler,
                    registration.handlerMethod(),
                    new Object[] {spot, message},
                    suspendHandlerInvokers)
                : ZLinkHandlerMethodInvoker.invokeHandler(
                    handler,
                    "handle",
                    new Object[] {spot, message},
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
        Object message = deserialize(payload, registration.messageType());
        try {
            Object handler = handlerFactory.create(registration.handlerType());
            CompletionStage<?> stage = registration.handlerMethod() != null
                ? ZLinkHandlerMethodInvoker.invoke(
                    handler,
                    registration.handlerMethod(),
                    new Object[] {spot, message},
                    suspendHandlerInvokers)
                : ZLinkHandlerMethodInvoker.invokeHandler(
                    handler,
                    "handle",
                    new Object[] {spot, message},
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
        return new Object[] {spot, actor, context, message, context.cancellationToken()};
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
                    new Object[] {
                        spotSurface,
                        actor,
                        context,
                        message,
                        context.cancellationToken()
                    },
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
                new Object[] {
                    spotSurface,
                    actor,
                    context,
                    message,
                    context.cancellationToken()
                },
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
