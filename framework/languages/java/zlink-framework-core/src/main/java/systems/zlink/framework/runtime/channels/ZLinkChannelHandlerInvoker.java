package systems.zlink.framework.runtime.channels;

import systems.zlink.framework.runtime.backend.*;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.Executor;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.locks.LockSupport;
import java.util.function.Supplier;
import java.util.logging.Logger;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.errors.ZlinkCloseException;
import systems.zlink.contracts.errors.ZlinkRecvException;
import systems.zlink.contracts.errors.ZlinkRequestException;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.framework.ZLinkHandlerContext;
import systems.zlink.framework.ZLinkHandlerFilter;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.channels.ZLinkFanoutClient;
import systems.zlink.framework.channels.ZLinkChannelRuntimeOptions;
import systems.zlink.framework.channels.ZLinkClientServerChannelRuntimeOptions;
import systems.zlink.framework.channels.ZLinkPublishCall;
import systems.zlink.framework.channels.ZLinkPublishContext;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.channels.ZLinkRequestCall;
import systems.zlink.framework.channels.ZLinkRouteRequestContext;
import systems.zlink.framework.channels.ZLinkRouteSendContext;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkSendCall;
import systems.zlink.framework.channels.ZLinkSendContext;
import systems.zlink.framework.channels.ZLinkSocketRuntimeOptions;
import systems.zlink.framework.configuration.ZLinkDispatchErrorAction;
import systems.zlink.framework.configuration.ZLinkDispatchErrorReason;
import systems.zlink.framework.configuration.ZLinkDispatchErrorSurface;
import systems.zlink.framework.configuration.ZLinkDispatchMessageKind;
import systems.zlink.framework.configuration.ZLinkMessageFlowEvent;
import systems.zlink.framework.configuration.ZLinkMessageFlowOutcome;
import systems.zlink.framework.configuration.ZLinkDispatchFailure;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.errors.ZLinkFrameworkErrorKind;
import systems.zlink.framework.errors.ZLinkFrameworkException;
import systems.zlink.framework.execution.ZLinkAsyncSerialQueue;
import systems.zlink.framework.locations.ZLinkLocationAutoConnectType;
import systems.zlink.framework.locations.ZLinkLocationRole;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventDispatcher;
import systems.zlink.framework.runtime.configuration.ZLinkCodecRegistration;
import systems.zlink.framework.runtime.configuration.ZLinkFrameworkRegistration;
import systems.zlink.framework.runtime.diagnostics.ZLinkDispatchErrorReporter;
import systems.zlink.framework.runtime.handlers.ZLinkFilterPipeline;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerScanner;
import systems.zlink.framework.runtime.internal.handlers.ZLinkHandlerActivator;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerMethodInvoker;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerStages;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandler;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandlerCatalog;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandlerKind;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandlerSurface;
import systems.zlink.framework.runtime.internal.handlers.ZLinkSuspendInvocationAdapter;
import systems.zlink.framework.runtime.messaging.ZLinkPayloadEncoding;
import systems.zlink.framework.runtime.messaging.ZLinkMessagePayloads;

final class ZLinkChannelHandlerInvoker {
    private final ZLinkMessageSerializer serializer;
    private final ZLinkCodecRegistration codecs;
    private final ZLinkHandlerActivator handlerFactory;
    private final Executor handlerExecutor;
    private final List<ZLinkSuspendInvocationAdapter> suspendHandlerInvokers;
    private final List<Class<? extends ZLinkHandlerFilter>> filterTypes;

    ZLinkChannelHandlerInvoker(
        ZLinkMessageSerializer serializer,
        ZLinkCodecRegistration codecs,
        ZLinkHandlerActivator handlerFactory,
        Executor handlerExecutor,
        List<ZLinkSuspendInvocationAdapter> suspendHandlerInvokers,
        List<Class<? extends ZLinkHandlerFilter>> filterTypes) {
        this.serializer = serializer;
        this.codecs = codecs;
        this.handlerFactory = handlerFactory;
        this.handlerExecutor = handlerExecutor;
        this.suspendHandlerInvokers = suspendHandlerInvokers;
        this.filterTypes = filterTypes;
    }

    <T> CompletionStage<T> executeHandler(
        java.util.function.Supplier<CompletionStage<T>> operation) {
        CompletableFuture<T> result = new CompletableFuture<>();
        var flow = systems.zlink.framework.runtime.internal.diagnostics.ZLinkFlowContext.current();
        try {
            handlerExecutor.execute(() -> {
                try {
                    operation.get().whenComplete((value, error) -> {
                        systems.zlink.framework.runtime.internal.diagnostics.ZLinkFlowContext.run(flow, () -> {
                            if (error != null) {
                                result.completeExceptionally(error);
                            } else {
                                result.complete(value);
                            }
                        });
                    });
                } catch (RuntimeException ex) {
                    result.completeExceptionally(ex);
                }
            });
        } catch (RuntimeException ex) {
            result.completeExceptionally(ex);
        }
        return result;
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    CompletionStage<Void> invokeSendHandler(
        String channelName,
        ChannelSendHandlerRegistration registration,
        Message payload) {
        return invokeSendHandler(channelName, registration, payload, Map.of());
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    CompletionStage<Void> invokeSendHandler(
        String channelName,
        ChannelSendHandlerRegistration registration,
        Message payload,
        Map<String, String> metadata) {
        Object message;
        try {
            message = ZLinkMessagePayloads.deserialize(serializer, payload, registration.messageType());
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(payloadDecodeFailure(
                channelName,
                registration.packetName(),
                ex));
        }
        try {
            ZLinkSendContext context = new DefaultSendContext(
                channelName,
                registration.packetName(),
                contentTypeFor(registration.messageType()),
                metadata);
            return invokeWithFilters(context, message, () ->
                invokeSendHandlerCore(registration, message, context));
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(ex);
        }
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    private CompletionStage<Void> invokeSendHandlerCore(
        ChannelSendHandlerRegistration registration,
        Object message,
        ZLinkSendContext context) {
        try {
            if (registration.handlerMethod() != null) {
                return invokeVoidMethodHandler(
                    registration.handlerType(),
                    registration.handlerMethod(),
                    message,
                    context);
            }
            Object handler = handlerFactory.create(registration.handlerType());
            return ZLinkHandlerMethodInvoker
                .invokeHandler(handler, "handle", new Object[] {message, context}, suspendHandlerInvokers)
                .thenApply(ignored -> null);
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(ex);
        }
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    CompletionStage<Message> invokeRequestHandler(
        String channelName,
        ChannelRequestHandlerRegistration registration,
        Message payload) {
        return invokeRequestHandler(channelName, registration, payload, Map.of());
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    CompletionStage<Message> invokeRequestHandler(
        String channelName,
        ChannelRequestHandlerRegistration registration,
        Message payload,
        Map<String, String> metadata) {
        Object request;
        try {
            request = ZLinkMessagePayloads.deserialize(serializer, payload, registration.requestType());
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(payloadDecodeFailure(
                channelName,
                registration.packetName(),
                ex));
        }
        try {
            ZLinkRequestContext context = new DefaultRequestContext(
                channelName,
                registration.packetName(),
                contentTypeFor(registration.requestType()),
                metadata);
            return invokeWithFilters(context, request, () ->
                invokeRequestHandlerCore(registration, request, context))
                .thenApply(reply -> ZLinkMessagePayloads.message(serializer.serialize(reply)));
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(ex);
        }
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    private CompletionStage<Object> invokeRequestHandlerCore(
        ChannelRequestHandlerRegistration registration,
        Object request,
        ZLinkRequestContext context) {
        try {
            if (registration.handlerMethod() != null) {
                return invokeReplyMethodHandler(
                    registration.handlerType(),
                    registration.handlerMethod(),
                    request,
                    context);
            }
            Object handler = handlerFactory.create(registration.handlerType());
            return ZLinkHandlerMethodInvoker
                .invokeHandler(handler, "handle", new Object[] {request, context}, suspendHandlerInvokers);
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(ex);
        }
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    CompletionStage<Void> invokePublishHandler(
        String channelName,
        ChannelPublishHandlerRegistration registration,
        String topic,
        Message payload) {
        Object message;
        try {
            message = ZLinkMessagePayloads.deserialize(serializer, payload, registration.messageType());
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(payloadDecodeFailure(
                channelName,
                registration.packetName(),
                ex));
        }
        try {
            ZLinkPublishContext context = new DefaultPublishContext(
                channelName,
                registration.packetName(),
                topic,
                contentTypeFor(registration.messageType()));
            return invokeWithFilters(context, message, () ->
                invokePublishHandlerCore(registration, message, context));
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(ex);
        }
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    private CompletionStage<Void> invokePublishHandlerCore(
        ChannelPublishHandlerRegistration registration,
        Object message,
        ZLinkPublishContext context) {
        try {
            if (registration.handlerMethod() != null) {
                return invokeVoidMethodHandler(
                    registration.handlerType(),
                    registration.handlerMethod(),
                    message,
                    context);
            }
            Object handler = handlerFactory.create(registration.handlerType());
            return ZLinkHandlerMethodInvoker
                .invokeHandler(handler, "handle", new Object[] {message, context}, suspendHandlerInvokers)
                .thenApply(ignored -> null);
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(ex);
        }
    }

    private CompletionStage<Void> invokeVoidMethodHandler(
        Class<?> handlerType,
        Method method,
        Object message,
        ZLinkHandlerContext context) {
        try {
            Object handler = handlerFactory.create(handlerType);
            return ZLinkHandlerMethodInvoker
                .invoke(handler, method, methodArguments(method, message, context), suspendHandlerInvokers)
                .thenApply(ignored -> null);
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "failed to invoke handler method: " + handlerType.getName() + "." + method.getName(),
                ex));
        }
    }

    private CompletionStage<Object> invokeReplyMethodHandler(
        Class<?> handlerType,
        Method method,
        Object message,
        ZLinkHandlerContext context) {
        try {
            Object handler = handlerFactory.create(handlerType);
            return ZLinkHandlerMethodInvoker.invoke(
                handler,
                method,
                methodArguments(method, message, context),
                suspendHandlerInvokers);
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "failed to invoke handler method: " + handlerType.getName() + "." + method.getName(),
                ex));
        }
    }

    static Object[] methodArguments(
        Method method,
        Object message,
        ZLinkHandlerContext context) {
        Class<?>[] parameterTypes = ZLinkHandlerMethodInvoker.logicalParameterTypes(method);
        Object[] arguments = new Object[parameterTypes.length];
        arguments[0] = message;
        for (int index = 1; index < parameterTypes.length; index++) {
            if (parameterTypes[index].isInstance(context)) {
                arguments[index] = context;
            } else {
                arguments[index] = null;
            }
        }
        return arguments;
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    CompletionStage<Void> invokeRouteSendHandler(
        String channelName,
        ChannelRouteSendHandlerRegistration registration,
        RoutingId sourceRoutingId,
        Message payload) {
        return invokeRouteSendHandler(
            channelName, registration, sourceRoutingId, payload, Map.of());
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    CompletionStage<Void> invokeRouteSendHandler(
        String channelName,
        ChannelRouteSendHandlerRegistration registration,
        RoutingId sourceRoutingId,
        Message payload,
        Map<String, String> metadata) {
        Object message;
        try {
            message = ZLinkMessagePayloads.deserialize(serializer, payload, registration.messageType());
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(payloadDecodeFailure(
                channelName,
                registration.packetName(),
                ex));
        }
        try {
            ZLinkRouteSendContext context =
                new DefaultRouteSendContext(
                    channelName,
                    registration.packetName(),
                    sourceRoutingId,
                    contentTypeFor(registration.messageType()),
                    metadata);
            if (registration.handlerMethod() != null) {
                Object handler = handlerFactory.create(registration.handlerType());
                return ZLinkHandlerMethodInvoker
                    .invoke(handler, registration.handlerMethod(),
                        methodArguments(registration.handlerMethod(), message, context),
                        suspendHandlerInvokers)
                    .thenApply(ignored -> null);
            }
            Object handler = handlerFactory.create(registration.handlerType());
            return ZLinkHandlerMethodInvoker
                .invokeHandler(handler, "handle", new Object[] {message, context}, suspendHandlerInvokers)
                .thenApply(ignored -> null);
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(ex);
        }
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    CompletionStage<Message> invokeRouteRequestHandler(
        String channelName,
        ChannelRouteRequestHandlerRegistration registration,
        RoutingId sourceRoutingId,
        Message payload) {
        return invokeRouteRequestHandler(
            channelName, registration, sourceRoutingId, payload, Map.of());
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    CompletionStage<Message> invokeRouteRequestHandler(
        String channelName,
        ChannelRouteRequestHandlerRegistration registration,
        RoutingId sourceRoutingId,
        Message payload,
        Map<String, String> metadata) {
        Object request;
        try {
            request = ZLinkMessagePayloads.deserialize(serializer, payload, registration.requestType());
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(payloadDecodeFailure(
                channelName,
                registration.packetName(),
                ex));
        }
        try {
            ZLinkRouteRequestContext context =
                new DefaultRouteRequestContext(
                    channelName,
                    registration.packetName(),
                    sourceRoutingId,
                    contentTypeFor(registration.requestType()),
                    metadata);
            if (registration.handlerMethod() != null) {
                Object handler = handlerFactory.create(registration.handlerType());
                return ZLinkHandlerMethodInvoker
                    .invoke(handler, registration.handlerMethod(),
                        methodArguments(registration.handlerMethod(), request, context),
                        suspendHandlerInvokers)
                    .thenApply(reply -> ZLinkMessagePayloads.message(serializer.serialize(reply)));
            }
            Object handler = handlerFactory.create(registration.handlerType());
            return ZLinkHandlerMethodInvoker
                .invokeHandler(handler, "handle", new Object[] {request, context}, suspendHandlerInvokers)
                .thenApply(reply -> ZLinkMessagePayloads.message(serializer.serialize(reply)));
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(ex);
        }
    }

    private static PayloadDecodeDispatchException payloadDecodeFailure(
        String channelName,
        String packetName,
        RuntimeException cause) {
        return new PayloadDecodeDispatchException(
            "PayloadDecodeFailed: failed to decode payload for '" + channelName + ":" + packetName + "'.",
            cause);
    }

    private String contentTypeFor(Class<?> payloadType) {
        return codecs.contentTypeFor(payloadType);
    }

    private <T> CompletionStage<T> invokeWithFilters(
        ZLinkHandlerContext context,
        Object request,
        java.util.function.Supplier<CompletionStage<T>> terminal) {
        if (filterTypes.isEmpty()) {
            return terminal.get();
        }
        return ZLinkFilterPipeline.invoke(
            filterTypes,
            handlerFactory,
            context,
            request,
            terminal);
    }
}
