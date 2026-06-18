package systems.zlink.framework.runtime.handlers;

import java.io.File;
import java.io.IOException;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.lang.reflect.ParameterizedType;
import java.lang.reflect.Type;
import java.net.JarURLConnection;
import java.net.URISyntaxException;
import java.net.URL;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Enumeration;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;
import java.util.jar.JarFile;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.ZLinkHandlerContext;
import systems.zlink.framework.channels.ZLinkPublishContext;
import systems.zlink.framework.channels.ZLinkPublishHandler;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.channels.ZLinkRouteRequestHandler;
import systems.zlink.framework.channels.ZLinkRouteSendHandler;
import systems.zlink.framework.channels.ZLinkSendContext;
import systems.zlink.framework.channels.ZLinkSendHandler;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkPacket;
import systems.zlink.framework.handlers.ZLinkPublish;
import systems.zlink.framework.handlers.ZLinkRequest;
import systems.zlink.framework.handlers.ZLinkSend;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.handlers.ZLinkSpotActorSend;
import systems.zlink.framework.handlers.ZLinkSpotRequest;
import systems.zlink.framework.handlers.ZLinkSpotSubscription;
import systems.zlink.framework.handlers.ZLinkSpotTimer;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.framework.spots.ZLinkSpotActorSendContext;
import systems.zlink.framework.spots.ZLinkEntrySpotActorRequestHandler;
import systems.zlink.framework.spots.ZLinkEntrySpotActorSendHandler;
import systems.zlink.framework.spots.ZLinkSpotActorRequestHandler;
import systems.zlink.framework.spots.ZLinkSpotActorSendHandler;
import systems.zlink.framework.spots.ZLinkSpotPacketHandler;
import systems.zlink.framework.spots.ZLinkSpotRequestHandler;
import systems.zlink.framework.spots.ZLinkSpotSubscriptionHandler;
import systems.zlink.framework.spots.ZLinkSpotTimerHandler;

public final class ZLinkHandlerScanner {
    private static final String KOTLIN_REQUEST_HANDLER =
        "systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler";
    private static final String KOTLIN_SEND_HANDLER =
        "systems.zlink.framework.kotlin.ZLinkSuspendingSendHandler";
    private static final String KOTLIN_PUBLISH_HANDLER =
        "systems.zlink.framework.kotlin.ZLinkSuspendingPublishHandler";
    private static final String KOTLIN_ROUTE_REQUEST_HANDLER =
        "systems.zlink.framework.kotlin.ZLinkSuspendingRouteRequestHandler";
    private static final String KOTLIN_ROUTE_SEND_HANDLER =
        "systems.zlink.framework.kotlin.ZLinkSuspendingRouteSendHandler";
    private static final String KOTLIN_SPOT_PACKET_HANDLER =
        "systems.zlink.framework.kotlin.ZLinkSuspendingSpotPacketHandler";
    private static final String KOTLIN_SPOT_REQUEST_HANDLER =
        "systems.zlink.framework.kotlin.ZLinkSuspendingSpotRequestHandler";
    private static final String KOTLIN_SPOT_SUBSCRIPTION_HANDLER =
        "systems.zlink.framework.kotlin.ZLinkSuspendingSpotSubscriptionHandler";
    private static final String KOTLIN_SPOT_TIMER_HANDLER =
        "systems.zlink.framework.kotlin.ZLinkSuspendingSpotTimerHandler";
    private static final String KOTLIN_ENTRY_SPOT_ACTOR_SEND_HANDLER =
        "systems.zlink.framework.kotlin.ZLinkSuspendingEntrySpotActorSendHandler";
    private static final String KOTLIN_ENTRY_SPOT_ACTOR_REQUEST_HANDLER =
        "systems.zlink.framework.kotlin.ZLinkSuspendingEntrySpotActorRequestHandler";
    private static final String KOTLIN_SPOT_ACTOR_SEND_HANDLER =
        "systems.zlink.framework.kotlin.ZLinkSuspendingSpotActorSendHandler";
    private static final String KOTLIN_SPOT_ACTOR_REQUEST_HANDLER =
        "systems.zlink.framework.kotlin.ZLinkSuspendingSpotActorRequestHandler";

    private ZLinkHandlerScanner() {
    }

    public static ZLinkScannedHandlerCatalog scan(Set<Class<?>> markerTypes) {
        if (markerTypes.isEmpty()) {
            return new ZLinkScannedHandlerCatalog(List.of());
        }

        Set<Class<?>> candidates = new LinkedHashSet<>();
        for (Class<?> markerType : markerTypes) {
            candidates.addAll(scanPackage(markerType));
        }

        List<ZLinkScannedHandler> handlers = new ArrayList<>();
        for (Class<?> candidate : candidates) {
            if (!isConcrete(candidate)) {
                continue;
            }
            Set<String> groups = resolveGroups(candidate);
            addMethodHandlers(handlers, candidate, groups);
            addInterfaceHandler(handlers, candidate, groups, ZLinkSendHandler.class,
                ZLinkScannedHandlerSurface.CHANNEL, ZLinkScannedHandlerKind.SEND);
            addInterfaceHandler(handlers, candidate, groups, KOTLIN_SEND_HANDLER,
                ZLinkScannedHandlerSurface.CHANNEL, ZLinkScannedHandlerKind.SEND);
            addInterfaceHandler(handlers, candidate, groups, ZLinkRequestHandler.class,
                ZLinkScannedHandlerSurface.CHANNEL, ZLinkScannedHandlerKind.REQUEST);
            addInterfaceHandler(handlers, candidate, groups, KOTLIN_REQUEST_HANDLER,
                ZLinkScannedHandlerSurface.CHANNEL, ZLinkScannedHandlerKind.REQUEST);
            addInterfaceHandler(handlers, candidate, groups, ZLinkPublishHandler.class,
                ZLinkScannedHandlerSurface.CHANNEL, ZLinkScannedHandlerKind.PUBLISH);
            addInterfaceHandler(handlers, candidate, groups, KOTLIN_PUBLISH_HANDLER,
                ZLinkScannedHandlerSurface.CHANNEL, ZLinkScannedHandlerKind.PUBLISH);
            addInterfaceHandler(handlers, candidate, groups, ZLinkRouteSendHandler.class,
                ZLinkScannedHandlerSurface.ROUTE, ZLinkScannedHandlerKind.SEND);
            addInterfaceHandler(handlers, candidate, groups, KOTLIN_ROUTE_SEND_HANDLER,
                ZLinkScannedHandlerSurface.ROUTE, ZLinkScannedHandlerKind.SEND);
            addInterfaceHandler(handlers, candidate, groups, ZLinkRouteRequestHandler.class,
                ZLinkScannedHandlerSurface.ROUTE, ZLinkScannedHandlerKind.REQUEST);
            addInterfaceHandler(handlers, candidate, groups, KOTLIN_ROUTE_REQUEST_HANDLER,
                ZLinkScannedHandlerSurface.ROUTE, ZLinkScannedHandlerKind.REQUEST);
            addSpotInterfaceHandlers(handlers, candidate, groups);
            addSpotActorInterfaceHandlers(handlers, candidate, groups);
        }
        return new ZLinkScannedHandlerCatalog(handlers);
    }

    private static void addMethodHandlers(
        List<ZLinkScannedHandler> handlers,
        Class<?> candidate,
        Set<String> groups) {
        for (Method method : candidate.getMethods()) {
            rejectConflictingSpotActorAnnotations(candidate, method);

            ZLinkSend send = method.getAnnotation(ZLinkSend.class);
            if (send != null) {
                rejectJavaCompletionStageReturn(candidate, method);
                Class<?> messageType = requireChannelHandlerShape(
                    candidate,
                    method,
                    ZLinkSendContext.class);
                handlers.add(new ZLinkScannedHandler(
                    ZLinkScannedHandlerSurface.CHANNEL,
                    ZLinkScannedHandlerKind.SEND,
                    candidate,
                    method,
                    messageType,
                    Void.class,
                    resolvePacketName(messageType, send.packetName()),
                    groups));
            }

            ZLinkRequest request = method.getAnnotation(ZLinkRequest.class);
            if (request != null) {
                rejectJavaCompletionStageReturn(candidate, method);
                Class<?> messageType = requireChannelHandlerShape(
                    candidate,
                    method,
                    ZLinkRequestContext.class);
                Class<?> replyType = resolveReplyType(candidate, method);
                handlers.add(new ZLinkScannedHandler(
                    ZLinkScannedHandlerSurface.CHANNEL,
                    ZLinkScannedHandlerKind.REQUEST,
                    candidate,
                    method,
                    messageType,
                    replyType,
                    resolvePacketName(messageType, request.packetName()),
                    groups));
            }

            ZLinkPublish publish = method.getAnnotation(ZLinkPublish.class);
            if (publish != null) {
                rejectJavaCompletionStageReturn(candidate, method);
                Class<?> messageType = requireChannelHandlerShape(
                    candidate,
                    method,
                    ZLinkPublishContext.class);
                handlers.add(new ZLinkScannedHandler(
                    ZLinkScannedHandlerSurface.CHANNEL,
                    ZLinkScannedHandlerKind.PUBLISH,
                    candidate,
                    method,
                    messageType,
                    Void.class,
                    resolvePacketName(messageType, publish.packetName()),
                    groups));
            }

            ZLinkSpotRequest spotRequest = method.getAnnotation(ZLinkSpotRequest.class);
            if (spotRequest != null) {
                rejectJavaCompletionStageReturn(candidate, method);
                SpotMethodShape shape = requireSpotMethodShape(
                    candidate,
                    method,
                    "SPOT request handler method must have spot and request parameters: ");
                Class<?> replyType = resolveReplyType(candidate, method);
                handlers.add(new ZLinkScannedHandler(
                    ZLinkScannedHandlerSurface.SPOT,
                    ZLinkScannedHandlerKind.REQUEST,
                    candidate,
                    method,
                    shape.spotType(),
                    shape.messageType(),
                    replyType,
                    resolvePacketName(shape.messageType(), spotRequest.packetName()),
                    "",
                    "",
                    null,
                    groups));
            }

            ZLinkSpotSubscription spotSubscription =
                method.getAnnotation(ZLinkSpotSubscription.class);
            if (spotSubscription != null) {
                rejectJavaCompletionStageReturn(candidate, method);
                SpotMethodShape shape = requireSpotMethodShape(
                    candidate,
                    method,
                    "SPOT subscription handler method must have spot and event parameters: ");
                handlers.add(new ZLinkScannedHandler(
                    ZLinkScannedHandlerSurface.SPOT,
                    ZLinkScannedHandlerKind.PUBLISH,
                    candidate,
                    method,
                    shape.spotType(),
                    shape.messageType(),
                    Void.class,
                    resolvePacketName(shape.messageType()),
                    requireTopic(candidate, spotSubscription.topic()),
                    "",
                    null,
                    groups));
            }

            ZLinkSpotActorSend actorSend = method.getAnnotation(ZLinkSpotActorSend.class);
            if (actorSend != null) {
                rejectJavaCompletionStageReturn(candidate, method);
                ActorMessageShape shape = requireActorPacketHandlerShape(
                    candidate,
                    method,
                    ZLinkSpotActorSendContext.class);
                handlers.add(new ZLinkScannedHandler(
                    ZLinkScannedHandlerSurface.SPOT,
                    ZLinkScannedHandlerKind.ACTOR_SEND,
                    candidate,
                    method,
                    shape.spotType(),
                    shape.messageType(),
                    Void.class,
                    resolvePacketName(shape.messageType(), actorSend.packetName()),
                    "",
                    "",
                    null,
                    groups));
            }

            ZLinkSpotActorRequest actorRequest = method.getAnnotation(ZLinkSpotActorRequest.class);
            if (actorRequest != null) {
                rejectJavaCompletionStageReturn(candidate, method);
                ActorMessageShape shape = requireActorPacketHandlerShape(
                    candidate,
                    method,
                    ZLinkSpotActorRequestContext.class);
                Class<?> replyType = resolveReplyType(candidate, method);
                handlers.add(new ZLinkScannedHandler(
                    ZLinkScannedHandlerSurface.SPOT,
                    ZLinkScannedHandlerKind.ACTOR_REQUEST,
                    candidate,
                    method,
                    shape.spotType(),
                    shape.messageType(),
                    replyType,
                    resolvePacketName(shape.messageType(), actorRequest.packetName()),
                    "",
                    "",
                    null,
                    groups));
            }

        }
    }

    private static SpotMethodShape requireSpotMethodShape(
        Class<?> handlerType,
        Method method,
        String failurePrefix) {
        Class<?>[] parameters = ZLinkHandlerMethodInvoker.logicalParameterTypes(method);
        if (parameters.length == 2) {
            return new SpotMethodShape(parameters[0], parameters[1]);
        }
        throw new ZLinkConfigurationException(
            failurePrefix + handlerType.getName() + "." + method.getName());
    }

    private record SpotMethodShape(Class<?> spotType, Class<?> messageType) {
    }

    private static void rejectJavaCompletionStageReturn(Class<?> handlerType, Method method) {
        if (ZLinkHandlerMethodInvoker.isKotlinSuspendMethod(method)) {
            return;
        }
        Type returnType = method.getGenericReturnType();
        if (returnType instanceof ParameterizedType parameterized
            && parameterized.getRawType() == java.util.concurrent.CompletionStage.class) {
            throw new ZLinkConfigurationException(
                "Java handler method must return a plain value or void, not CompletionStage: "
                    + handlerType.getName() + "." + method.getName());
        }
    }

    private static void rejectConflictingSpotActorAnnotations(Class<?> handlerType, Method method) {
        if (method.getAnnotation(ZLinkSpotActorSend.class) != null
            && method.getAnnotation(ZLinkSpotActorRequest.class) != null) {
            throw new ZLinkConfigurationException(
                "SPOT actor handler method cannot declare both send and request annotations: "
                    + handlerType.getName() + "." + method.getName());
        }
    }

    private static ActorMessageShape requireActorPacketHandlerShape(
        Class<?> handlerType,
        Method method,
        Class<? extends ZLinkHandlerContext> contextType) {
        Class<?>[] parameters = ZLinkHandlerMethodInvoker.logicalParameterTypes(method);
        if (parameters.length == 2) {
            return new ActorMessageShape(null, parameters[0], parameters[1]);
        }
        if (parameters.length == 5
            && parameters[2].isAssignableFrom(contextType)
            && parameters[4] == CancellationToken.class) {
            return new ActorMessageShape(parameters[0], parameters[1], parameters[3]);
        }
        throw new ZLinkConfigurationException(
            "Spot actor packet handler method must have actor/message or spot, actor, context, message, CancellationToken parameters: "
                + handlerType.getName() + "." + method.getName());
    }

    private record ActorMessageShape(Class<?> spotType, Class<?> actorType, Class<?> messageType) {
    }

    private static Set<Class<?>> scanPackage(Class<?> markerType) {
        String packageName = markerType.getPackageName();
        String packagePath = packageName.replace('.', '/');
        ClassLoader loader = markerType.getClassLoader();
        Set<Class<?>> classes = new LinkedHashSet<>();
        try {
            Enumeration<URL> resources = loader.getResources(packagePath);
            while (resources.hasMoreElements()) {
                URL resource = resources.nextElement();
                if ("file".equals(resource.getProtocol())) {
                    scanDirectory(loader, packageName, new File(resource.toURI()), classes);
                } else if ("jar".equals(resource.getProtocol())) {
                    scanJar(loader, packagePath, resource, classes);
                }
            }
        } catch (IOException | URISyntaxException ex) {
            throw new ZLinkConfigurationException(
                "failed to scan handler package: " + packageName);
        }
        return classes;
    }

    private static void scanDirectory(
        ClassLoader loader,
        String packageName,
        File directory,
        Set<Class<?>> classes) {
        File[] files = directory.listFiles();
        if (files == null) {
            return;
        }
        for (File file : files) {
            if (file.isDirectory()) {
                scanDirectory(loader, packageName + "." + file.getName(), file, classes);
            } else if (file.getName().endsWith(".class")) {
                String simpleName = file.getName().substring(0, file.getName().length() - 6);
                loadClass(loader, packageName + "." + simpleName, classes);
            }
        }
    }

    private static void scanJar(
        ClassLoader loader,
        String packagePath,
        URL resource,
        Set<Class<?>> classes) throws IOException {
        JarURLConnection connection = (JarURLConnection) resource.openConnection();
        try (JarFile jar = connection.getJarFile()) {
            jar.stream()
                .filter(entry -> !entry.isDirectory())
                .map(entry -> entry.getName())
                .filter(name -> name.startsWith(packagePath) && name.endsWith(".class"))
                .map(name -> name.substring(0, name.length() - 6).replace('/', '.'))
                .forEach(className -> loadClass(loader, className, classes));
        }
    }

    private static void loadClass(ClassLoader loader, String className, Set<Class<?>> classes) {
        try {
            classes.add(Class.forName(className, false, loader));
        } catch (ClassNotFoundException | NoClassDefFoundError ignored) {
            // Optional dependencies in the same package should not make registration unusable.
        }
    }

    private static boolean isConcrete(Class<?> type) {
        int modifiers = type.getModifiers();
        return !type.isInterface()
            && !type.isAnnotation()
            && !type.isEnum()
            && !Modifier.isAbstract(modifiers);
    }

    private static Set<String> resolveGroups(Class<?> type) {
        ZLinkHandlerGroup[] groups = type.getAnnotationsByType(ZLinkHandlerGroup.class);
        if (groups.length == 0) {
            return Set.of();
        }
        Set<String> resolved = new LinkedHashSet<>();
        for (ZLinkHandlerGroup group : groups) {
            if (!group.value().isBlank()) {
                resolved.add(group.value());
            }
        }
        return Set.copyOf(resolved);
    }

    @SuppressWarnings("rawtypes")
    private static void addInterfaceHandler(
        List<ZLinkScannedHandler> handlers,
        Class<?> candidate,
        Set<String> groups,
        Class<?> handlerInterface,
        ZLinkScannedHandlerSurface surface,
        ZLinkScannedHandlerKind kind) {
        ParameterizedType matched = findInterface(candidate, handlerInterface);
        if (matched == null) {
            return;
        }
        Type[] arguments = matched.getActualTypeArguments();
        Class<?> messageType = requireClassArgument(candidate, arguments[0]);
        Class<?> replyType = kind == ZLinkScannedHandlerKind.REQUEST
            ? requireClassArgument(candidate, arguments[1])
            : Void.class;
        handlers.add(new ZLinkScannedHandler(
            surface,
            kind,
            candidate,
            messageType,
            replyType,
            resolvePacketName(messageType),
            groups));
    }

    private static void addInterfaceHandler(
        List<ZLinkScannedHandler> handlers,
        Class<?> candidate,
        Set<String> groups,
        String handlerInterfaceName,
        ZLinkScannedHandlerSurface surface,
        ZLinkScannedHandlerKind kind) {
        ParameterizedType matched = findInterface(candidate, handlerInterfaceName);
        if (matched == null) {
            return;
        }
        Type[] arguments = matched.getActualTypeArguments();
        Class<?> messageType = requireClassArgument(candidate, arguments[0]);
        Class<?> replyType = kind == ZLinkScannedHandlerKind.REQUEST
            ? requireClassArgument(candidate, arguments[1])
            : Void.class;
        handlers.add(new ZLinkScannedHandler(
            surface,
            kind,
            candidate,
            messageType,
            replyType,
            resolvePacketName(messageType),
            groups));
    }

    private static void addSpotInterfaceHandlers(
        List<ZLinkScannedHandler> handlers,
        Class<?> candidate,
        Set<String> groups) {
        addSpotPacketInterfaceHandler(
            handlers,
            candidate,
            groups,
            ZLinkSpotPacketHandler.class,
            ZLinkScannedHandlerKind.SEND);
        addSpotPacketInterfaceHandler(
            handlers,
            candidate,
            groups,
            KOTLIN_SPOT_PACKET_HANDLER,
            ZLinkScannedHandlerKind.SEND);
        addSpotPacketInterfaceHandler(
            handlers,
            candidate,
            groups,
            ZLinkSpotRequestHandler.class,
            ZLinkScannedHandlerKind.REQUEST);
        addSpotPacketInterfaceHandler(
            handlers,
            candidate,
            groups,
            KOTLIN_SPOT_REQUEST_HANDLER,
            ZLinkScannedHandlerKind.REQUEST);
        addSpotSubscriptionInterfaceHandler(handlers, candidate, groups);
        addSpotSubscriptionInterfaceHandler(handlers, candidate, groups, KOTLIN_SPOT_SUBSCRIPTION_HANDLER);
        addSpotTimerInterfaceHandler(handlers, candidate, groups);
        addSpotTimerInterfaceHandler(handlers, candidate, groups, KOTLIN_SPOT_TIMER_HANDLER);
    }

    private static void addSpotPacketInterfaceHandler(
        List<ZLinkScannedHandler> handlers,
        Class<?> candidate,
        Set<String> groups,
        Class<?> handlerInterface,
        ZLinkScannedHandlerKind kind) {
        ParameterizedType matched = findInterface(candidate, handlerInterface);
        if (matched == null) {
            return;
        }
        Type[] arguments = matched.getActualTypeArguments();
        Class<?> spotType = requireClassArgument(candidate, arguments[0]);
        Class<?> messageType = requireClassArgument(candidate, arguments[1]);
        Class<?> replyType = kind == ZLinkScannedHandlerKind.REQUEST
            ? requireClassArgument(candidate, arguments[2])
            : Void.class;
        handlers.add(new ZLinkScannedHandler(
            ZLinkScannedHandlerSurface.SPOT,
            kind,
            candidate,
            null,
            spotType,
            messageType,
            replyType,
            resolvePacketName(messageType),
            "",
            "",
            null,
            groups));
    }

    private static void addSpotPacketInterfaceHandler(
        List<ZLinkScannedHandler> handlers,
        Class<?> candidate,
        Set<String> groups,
        String handlerInterfaceName,
        ZLinkScannedHandlerKind kind) {
        ParameterizedType matched = findInterface(candidate, handlerInterfaceName);
        if (matched == null) {
            return;
        }
        Type[] arguments = matched.getActualTypeArguments();
        Class<?> spotType = requireClassArgument(candidate, arguments[0]);
        Class<?> messageType = requireClassArgument(candidate, arguments[1]);
        Class<?> replyType = kind == ZLinkScannedHandlerKind.REQUEST
            ? requireClassArgument(candidate, arguments[2])
            : Void.class;
        handlers.add(new ZLinkScannedHandler(
            ZLinkScannedHandlerSurface.SPOT,
            kind,
            candidate,
            null,
            spotType,
            messageType,
            replyType,
            resolvePacketName(messageType),
            "",
            "",
            null,
            groups));
    }

    private static void addSpotSubscriptionInterfaceHandler(
        List<ZLinkScannedHandler> handlers,
        Class<?> candidate,
        Set<String> groups) {
        ParameterizedType matched = findInterface(candidate, ZLinkSpotSubscriptionHandler.class);
        if (matched == null) {
            return;
        }
        ZLinkSpotSubscription annotation = candidate.getAnnotation(ZLinkSpotSubscription.class);
        if (annotation == null) {
            return;
        }
        Type[] arguments = matched.getActualTypeArguments();
        Class<?> spotType = requireClassArgument(candidate, arguments[0]);
        Class<?> messageType = requireClassArgument(candidate, arguments[1]);
        handlers.add(new ZLinkScannedHandler(
            ZLinkScannedHandlerSurface.SPOT,
            ZLinkScannedHandlerKind.PUBLISH,
            candidate,
            null,
            spotType,
            messageType,
            Void.class,
            resolvePacketName(messageType),
            requireTopic(candidate, annotation.topic()),
            "",
            null,
            groups));
    }

    private static void addSpotSubscriptionInterfaceHandler(
        List<ZLinkScannedHandler> handlers,
        Class<?> candidate,
        Set<String> groups,
        String handlerInterfaceName) {
        ParameterizedType matched = findInterface(candidate, handlerInterfaceName);
        if (matched == null) {
            return;
        }
        ZLinkSpotSubscription annotation = candidate.getAnnotation(ZLinkSpotSubscription.class);
        if (annotation == null) {
            return;
        }
        Type[] arguments = matched.getActualTypeArguments();
        Class<?> spotType = requireClassArgument(candidate, arguments[0]);
        Class<?> messageType = requireClassArgument(candidate, arguments[1]);
        handlers.add(new ZLinkScannedHandler(
            ZLinkScannedHandlerSurface.SPOT,
            ZLinkScannedHandlerKind.PUBLISH,
            candidate,
            null,
            spotType,
            messageType,
            Void.class,
            resolvePacketName(messageType),
            requireTopic(candidate, annotation.topic()),
            "",
            null,
            groups));
    }

    private static void addSpotTimerInterfaceHandler(
        List<ZLinkScannedHandler> handlers,
        Class<?> candidate,
        Set<String> groups) {
        ParameterizedType matched = findInterface(candidate, ZLinkSpotTimerHandler.class);
        if (matched == null) {
            return;
        }
        ZLinkSpotTimer annotation = candidate.getAnnotation(ZLinkSpotTimer.class);
        if (annotation == null) {
            return;
        }
        if (annotation.name().isBlank()) {
            throw new ZLinkConfigurationException(
                "SPOT timer handler name is required: " + candidate.getName());
        }
        if (annotation.periodMillis() <= 0) {
            throw new ZLinkConfigurationException(
                "SPOT timer period must be positive: " + candidate.getName());
        }
        Type[] arguments = matched.getActualTypeArguments();
        Class<?> spotType = requireClassArgument(candidate, arguments[0]);
        handlers.add(new ZLinkScannedHandler(
            ZLinkScannedHandlerSurface.SPOT,
            ZLinkScannedHandlerKind.TIMER,
            candidate,
            null,
            spotType,
            Void.class,
            Void.class,
            "",
            "",
            annotation.name(),
            Duration.ofMillis(annotation.periodMillis()),
            groups));
    }

    private static void addSpotTimerInterfaceHandler(
        List<ZLinkScannedHandler> handlers,
        Class<?> candidate,
        Set<String> groups,
        String handlerInterfaceName) {
        ParameterizedType matched = findInterface(candidate, handlerInterfaceName);
        if (matched == null) {
            return;
        }
        ZLinkSpotTimer annotation = candidate.getAnnotation(ZLinkSpotTimer.class);
        if (annotation == null) {
            return;
        }
        if (annotation.name().isBlank()) {
            throw new ZLinkConfigurationException(
                "SPOT timer handler name is required: " + candidate.getName());
        }
        if (annotation.periodMillis() <= 0) {
            throw new ZLinkConfigurationException(
                "SPOT timer period must be positive: " + candidate.getName());
        }
        Type[] arguments = matched.getActualTypeArguments();
        Class<?> spotType = requireClassArgument(candidate, arguments[0]);
        handlers.add(new ZLinkScannedHandler(
            ZLinkScannedHandlerSurface.SPOT,
            ZLinkScannedHandlerKind.TIMER,
            candidate,
            null,
            spotType,
            Void.class,
            Void.class,
            "",
            "",
            annotation.name(),
            Duration.ofMillis(annotation.periodMillis()),
            groups));
    }

    private static void addSpotActorInterfaceHandlers(
        List<ZLinkScannedHandler> handlers,
        Class<?> candidate,
        Set<String> groups) {
        addSpotActorPacketInterfaceHandler(
            handlers,
            candidate,
            groups,
            ZLinkEntrySpotActorSendHandler.class,
            ZLinkScannedHandlerKind.ACTOR_SEND);
        addSpotActorPacketInterfaceHandler(
            handlers,
            candidate,
            groups,
            KOTLIN_ENTRY_SPOT_ACTOR_SEND_HANDLER,
            ZLinkScannedHandlerKind.ACTOR_SEND);
        addSpotActorPacketInterfaceHandler(
            handlers,
            candidate,
            groups,
            ZLinkEntrySpotActorRequestHandler.class,
            ZLinkScannedHandlerKind.ACTOR_REQUEST);
        addSpotActorPacketInterfaceHandler(
            handlers,
            candidate,
            groups,
            KOTLIN_ENTRY_SPOT_ACTOR_REQUEST_HANDLER,
            ZLinkScannedHandlerKind.ACTOR_REQUEST);
        addSpotActorPacketInterfaceHandler(
            handlers,
            candidate,
            groups,
            ZLinkSpotActorSendHandler.class,
            ZLinkScannedHandlerKind.ACTOR_SEND);
        addSpotActorPacketInterfaceHandler(
            handlers,
            candidate,
            groups,
            KOTLIN_SPOT_ACTOR_SEND_HANDLER,
            ZLinkScannedHandlerKind.ACTOR_SEND);
        addSpotActorPacketInterfaceHandler(
            handlers,
            candidate,
            groups,
            ZLinkSpotActorRequestHandler.class,
            ZLinkScannedHandlerKind.ACTOR_REQUEST);
        addSpotActorPacketInterfaceHandler(
            handlers,
            candidate,
            groups,
            KOTLIN_SPOT_ACTOR_REQUEST_HANDLER,
            ZLinkScannedHandlerKind.ACTOR_REQUEST);
    }

    private static void addSpotActorPacketInterfaceHandler(
        List<ZLinkScannedHandler> handlers,
        Class<?> candidate,
        Set<String> groups,
        Class<?> handlerInterface,
        ZLinkScannedHandlerKind kind) {
        ParameterizedType matched = findInterface(candidate, handlerInterface);
        if (matched == null) {
            return;
        }
        Type[] arguments = matched.getActualTypeArguments();
        Class<?> spotType = requireClassArgument(candidate, arguments[0]);
        Class<?> messageType = requireClassArgument(candidate, arguments[2]);
        Class<?> replyType = kind == ZLinkScannedHandlerKind.ACTOR_REQUEST
            ? requireClassArgument(candidate, arguments[3])
            : Void.class;
        handlers.add(new ZLinkScannedHandler(
            ZLinkScannedHandlerSurface.SPOT,
            kind,
            candidate,
            null,
            spotType,
            messageType,
            replyType,
            resolvePacketName(messageType),
            "",
            "",
            null,
            groups));
    }

    private static void addSpotActorPacketInterfaceHandler(
        List<ZLinkScannedHandler> handlers,
        Class<?> candidate,
        Set<String> groups,
        String handlerInterfaceName,
        ZLinkScannedHandlerKind kind) {
        ParameterizedType matched = findInterface(candidate, handlerInterfaceName);
        if (matched == null) {
            return;
        }
        Type[] arguments = matched.getActualTypeArguments();
        Class<?> spotType = requireClassArgument(candidate, arguments[0]);
        Class<?> messageType = requireClassArgument(candidate, arguments[2]);
        Class<?> replyType = kind == ZLinkScannedHandlerKind.ACTOR_REQUEST
            ? requireClassArgument(candidate, arguments[3])
            : Void.class;
        handlers.add(new ZLinkScannedHandler(
            ZLinkScannedHandlerSurface.SPOT,
            kind,
            candidate,
            null,
            spotType,
            messageType,
            replyType,
            resolvePacketName(messageType),
            "",
            "",
            null,
            groups));
    }

    private static void addSpotActorLifecycleInterfaceHandler(
        List<ZLinkScannedHandler> handlers,
        Class<?> candidate,
        Set<String> groups,
        Class<?> handlerInterface,
        ZLinkScannedHandlerKind kind) {
        ParameterizedType matched = findInterface(candidate, handlerInterface);
        if (matched == null) {
            return;
        }
        Type[] arguments = matched.getActualTypeArguments();
        Class<?> actorType = requireClassArgument(candidate, arguments[1]);
        handlers.add(new ZLinkScannedHandler(
            ZLinkScannedHandlerSurface.SPOT,
            kind,
            candidate,
            actorType,
            Void.class,
            "",
            groups));
    }

    private static Class<?> requireChannelHandlerShape(
        Class<?> handlerType,
        Method method,
        Class<? extends ZLinkHandlerContext> contextType) {
        Class<?>[] parameters = ZLinkHandlerMethodInvoker.logicalParameterTypes(method);
        if (parameters.length == 0) {
            throw new ZLinkConfigurationException(
                "handler method must have a message parameter: "
                    + handlerType.getName() + "." + method.getName());
        }
        for (int index = 1; index < parameters.length; index++) {
            if (parameters[index] == CancellationToken.class
                || parameters[index].isAssignableFrom(contextType)) {
                continue;
            }
        }
        return parameters[0];
    }

    private static Class<?> resolveReplyType(Class<?> handlerType, Method method) {
        if (ZLinkHandlerMethodInvoker.isKotlinSuspendMethod(method)) {
            return ZLinkHandlerMethodInvoker.kotlinSuspendReplyType(handlerType, method);
        }
        if (method.getReturnType() == Void.TYPE || method.getReturnType() == Void.class) {
            throw new ZLinkConfigurationException(
                "request handler method must return a reply: "
                    + handlerType.getName() + "." + method.getName());
        }
        return method.getReturnType();
    }

    private static ParameterizedType findInterface(Class<?> type, Class<?> targetRawType) {
        return ZLinkGenericTypeResolver.findInterface(type, targetRawType);
    }

    private static ParameterizedType findInterface(Class<?> type, String targetRawTypeName) {
        return ZLinkGenericTypeResolver.findInterface(type, targetRawTypeName);
    }

    private static Class<?> requireClassArgument(Class<?> handlerType, Type argument) {
        return ZLinkGenericTypeResolver.requireClassArgument(handlerType, argument);
    }

    private static String resolvePacketName(Class<?> messageType) {
        ZLinkPacket packet = messageType.getAnnotation(ZLinkPacket.class);
        return packet == null ? messageType.getSimpleName() : packet.value();
    }

    private static String resolvePacketName(Class<?> messageType, String explicitPacketName) {
        return explicitPacketName == null || explicitPacketName.isBlank()
            ? resolvePacketName(messageType)
            : explicitPacketName;
    }

    private static String requireTopic(Class<?> handlerType, String topic) {
        if (topic == null || topic.isBlank()) {
            throw new ZLinkConfigurationException(
                "SPOT subscription handler topic is required: " + handlerType.getName());
        }
        return topic;
    }
}
