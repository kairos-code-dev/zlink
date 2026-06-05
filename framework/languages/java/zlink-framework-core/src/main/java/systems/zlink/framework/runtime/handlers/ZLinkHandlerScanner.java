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
import systems.zlink.framework.handlers.ZLinkSpotActorJoin;
import systems.zlink.framework.handlers.ZLinkSpotActorDisconnected;
import systems.zlink.framework.handlers.ZLinkSpotActorLeft;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.handlers.ZLinkSpotActorSend;
import systems.zlink.framework.handlers.ZLinkSpotPostActorJoined;
import systems.zlink.framework.spots.ZLinkSpotActorChangeResult;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.framework.spots.ZLinkSpotActorSendContext;

public final class ZLinkHandlerScanner {
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
            addInterfaceHandler(handlers, candidate, groups, ZLinkRequestHandler.class,
                ZLinkScannedHandlerSurface.CHANNEL, ZLinkScannedHandlerKind.REQUEST);
            addInterfaceHandler(handlers, candidate, groups, ZLinkPublishHandler.class,
                ZLinkScannedHandlerSurface.CHANNEL, ZLinkScannedHandlerKind.PUBLISH);
            addInterfaceHandler(handlers, candidate, groups, ZLinkRouteSendHandler.class,
                ZLinkScannedHandlerSurface.ROUTE, ZLinkScannedHandlerKind.SEND);
            addInterfaceHandler(handlers, candidate, groups, ZLinkRouteRequestHandler.class,
                ZLinkScannedHandlerSurface.ROUTE, ZLinkScannedHandlerKind.REQUEST);
        }
        return new ZLinkScannedHandlerCatalog(handlers);
    }

    private static void addMethodHandlers(
        List<ZLinkScannedHandler> handlers,
        Class<?> candidate,
        Set<String> groups) {
        for (Method method : candidate.getMethods()) {
            ZLinkSend send = method.getAnnotation(ZLinkSend.class);
            if (send != null) {
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

            ZLinkSpotActorJoin actorJoin = method.getAnnotation(ZLinkSpotActorJoin.class);
            if (actorJoin != null) {
                ActorMessageShape shape = requireActorJoinHandlerShape(candidate, method);
                Class<?> replyType = resolveReplyType(candidate, method);
                handlers.add(new ZLinkScannedHandler(
                    ZLinkScannedHandlerSurface.SPOT,
                    ZLinkScannedHandlerKind.ACTOR_JOIN,
                    candidate,
                    method,
                    shape.messageType(),
                    replyType,
                    resolvePacketName(shape.messageType()),
                    groups));
            }

            ZLinkSpotActorSend actorSend = method.getAnnotation(ZLinkSpotActorSend.class);
            if (actorSend != null) {
                ActorMessageShape shape = requireActorPacketHandlerShape(
                    candidate,
                    method,
                    ZLinkSpotActorSendContext.class);
                handlers.add(new ZLinkScannedHandler(
                    ZLinkScannedHandlerSurface.SPOT,
                    ZLinkScannedHandlerKind.ACTOR_SEND,
                    candidate,
                    method,
                    shape.messageType(),
                    Void.class,
                    resolvePacketName(shape.messageType(), actorSend.packetName()),
                    groups));
            }

            ZLinkSpotActorRequest actorRequest = method.getAnnotation(ZLinkSpotActorRequest.class);
            if (actorRequest != null) {
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
                    shape.messageType(),
                    replyType,
                    resolvePacketName(shape.messageType(), actorRequest.packetName()),
                    groups));
            }

            ZLinkSpotPostActorJoined postActorJoined =
                method.getAnnotation(ZLinkSpotPostActorJoined.class);
            if (postActorJoined != null) {
                Class<?> actorType = requireActorLifecycleHandlerShape(candidate, method);
                handlers.add(new ZLinkScannedHandler(
                    ZLinkScannedHandlerSurface.SPOT,
                    ZLinkScannedHandlerKind.ACTOR_JOINED,
                    candidate,
                    method,
                    actorType,
                    Void.class,
                    "",
                    groups));
            }

            ZLinkSpotActorLeft actorLeft = method.getAnnotation(ZLinkSpotActorLeft.class);
            if (actorLeft != null) {
                Class<?> actorType = requireActorLifecycleHandlerShape(candidate, method);
                handlers.add(new ZLinkScannedHandler(
                    ZLinkScannedHandlerSurface.SPOT,
                    ZLinkScannedHandlerKind.ACTOR_LEFT,
                    candidate,
                    method,
                    actorType,
                    Void.class,
                    "",
                    groups));
            }

            ZLinkSpotActorDisconnected actorDisconnected =
                method.getAnnotation(ZLinkSpotActorDisconnected.class);
            if (actorDisconnected != null) {
                Class<?> actorType = requireActorDisconnectedHandlerShape(candidate, method);
                handlers.add(new ZLinkScannedHandler(
                    ZLinkScannedHandlerSurface.SPOT,
                    ZLinkScannedHandlerKind.ACTOR_DISCONNECTED,
                    candidate,
                    method,
                    actorType,
                    Void.class,
                    "",
                    groups));
            }
        }
    }

    private static Class<?> requireActorDisconnectedHandlerShape(Class<?> handlerType, Method method) {
        Class<?>[] parameters = method.getParameterTypes();
        if (parameters.length == 1) {
            return parameters[0];
        }
        if (parameters.length == 3 && parameters[2] == CancellationToken.class) {
            return parameters[1];
        }
        throw new ZLinkConfigurationException(
            "Spot actor disconnected handler method must have actor parameter or spot, actor, CancellationToken parameters: "
                + handlerType.getName() + "." + method.getName());
    }

    private static Class<?> requireActorLifecycleHandlerShape(Class<?> handlerType, Method method) {
        Class<?>[] parameters = method.getParameterTypes();
        if (parameters.length == 2 && parameters[1] == ZLinkSpotActorChangeResult.class) {
            return parameters[0];
        }
        if (parameters.length == 4
            && parameters[2] == ZLinkSpotActorChangeResult.class
            && parameters[3] == CancellationToken.class) {
            return parameters[1];
        }
        throw new ZLinkConfigurationException(
            "Spot actor lifecycle handler method must have actor/change or spot, actor, change, CancellationToken parameters: "
                + handlerType.getName() + "." + method.getName());
    }

    private static ActorMessageShape requireActorJoinHandlerShape(Class<?> handlerType, Method method) {
        Class<?>[] parameters = method.getParameterTypes();
        if (parameters.length == 2) {
            return new ActorMessageShape(parameters[0], parameters[1]);
        }
        if (parameters.length == 4 && parameters[3] == CancellationToken.class) {
            return new ActorMessageShape(parameters[1], parameters[2]);
        }
        throw new ZLinkConfigurationException(
            "Spot actor join handler method must have actor/request or spot, actor, request, CancellationToken parameters: "
                + handlerType.getName() + "." + method.getName());
    }

    private static ActorMessageShape requireActorPacketHandlerShape(
        Class<?> handlerType,
        Method method,
        Class<? extends ZLinkHandlerContext> contextType) {
        Class<?>[] parameters = method.getParameterTypes();
        if (parameters.length == 2) {
            return new ActorMessageShape(parameters[0], parameters[1]);
        }
        if (parameters.length == 5
            && parameters[2].isAssignableFrom(contextType)
            && parameters[4] == CancellationToken.class) {
            return new ActorMessageShape(parameters[1], parameters[3]);
        }
        throw new ZLinkConfigurationException(
            "Spot actor packet handler method must have actor/message or spot, actor, context, message, CancellationToken parameters: "
                + handlerType.getName() + "." + method.getName());
    }

    private record ActorMessageShape(Class<?> actorType, Class<?> messageType) {
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

    private static Class<?> requireChannelHandlerShape(
        Class<?> handlerType,
        Method method,
        Class<? extends ZLinkHandlerContext> contextType) {
        Class<?>[] parameters = method.getParameterTypes();
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
            throw new ZLinkConfigurationException(
                "handler method parameter must be a matching context or CancellationToken: "
                    + handlerType.getName() + "." + method.getName());
        }
        return parameters[0];
    }

    private static Class<?> resolveReplyType(Class<?> handlerType, Method method) {
        Type returnType = method.getGenericReturnType();
        if (returnType instanceof ParameterizedType parameterized
            && parameterized.getRawType() == java.util.concurrent.CompletionStage.class) {
            return requireClassArgument(handlerType, parameterized.getActualTypeArguments()[0]);
        }
        if (method.getReturnType() == Void.TYPE || method.getReturnType() == Void.class) {
            throw new ZLinkConfigurationException(
                "request handler method must return a reply: "
                    + handlerType.getName() + "." + method.getName());
        }
        return method.getReturnType();
    }

    private static ParameterizedType findInterface(Class<?> type, Class<?> targetRawType) {
        for (Type interfaceType : type.getGenericInterfaces()) {
            ParameterizedType matched = matchInterface(interfaceType, targetRawType);
            if (matched != null) {
                return matched;
            }
        }
        Class<?> superclass = type.getSuperclass();
        return superclass == null || superclass == Object.class
            ? null
            : findInterface(superclass, targetRawType);
    }

    private static ParameterizedType matchInterface(Type interfaceType, Class<?> targetRawType) {
        if (interfaceType instanceof ParameterizedType parameterized
            && parameterized.getRawType() == targetRawType) {
            return parameterized;
        }
        if (interfaceType instanceof Class<?> raw) {
            return findInterface(raw, targetRawType);
        }
        return null;
    }

    private static Class<?> requireClassArgument(Class<?> handlerType, Type argument) {
        if (argument instanceof Class<?> klass) {
            return klass;
        }
        if (argument instanceof ParameterizedType parameterized
            && parameterized.getRawType() instanceof Class<?> raw) {
            return raw;
        }
        throw new ZLinkConfigurationException(
            "handler generic argument must resolve to a class: " + handlerType.getName());
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
}
