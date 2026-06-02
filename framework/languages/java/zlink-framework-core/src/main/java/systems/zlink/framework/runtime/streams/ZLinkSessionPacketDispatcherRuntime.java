package systems.zlink.framework.runtime.streams;

import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionPacketDispatcher;
import systems.zlink.framework.streams.ZLinkSessionPacketHandler;
import systems.zlink.framework.streams.ZLinkStreamHeader;

final class ZLinkSessionPacketDispatcherRuntime<TSessionContext extends ZLinkSessionContext>
    implements ZLinkSessionPacketDispatcher<TSessionContext> {
    private final Map<String, ZLinkSessionPacketHandler<TSessionContext>> handlers;

    ZLinkSessionPacketDispatcherRuntime(
        List<Class<? extends ZLinkSessionPacketHandler<?>>> handlerTypes,
        ZLinkHandlerFactory handlerFactory) {
        this.handlers = buildHandlerMap(handlerTypes, handlerFactory);
    }

    @Override
    public CompletionStage<Boolean> tryHandleAsync(
        TSessionContext context,
        ZLinkStreamHeader header,
        Message payload) {
        ZLinkSessionPacketHandler<TSessionContext> handler =
            handlers.get(header.packetName());
        if (handler == null) {
            return java.util.concurrent.CompletableFuture.completedFuture(false);
        }
        return handler.handleAsync(context, header, payload)
            .thenApply(ignored -> true);
    }

    private static <TSessionContext extends ZLinkSessionContext>
    Map<String, ZLinkSessionPacketHandler<TSessionContext>> buildHandlerMap(
        List<Class<? extends ZLinkSessionPacketHandler<?>>> handlerTypes,
        ZLinkHandlerFactory handlerFactory) {
        Map<String, ZLinkSessionPacketHandler<TSessionContext>> map =
            new HashMap<>();
        for (Class<? extends ZLinkSessionPacketHandler<?>> handlerType : handlerTypes) {
            @SuppressWarnings("unchecked")
            ZLinkSessionPacketHandler<TSessionContext> handler =
                (ZLinkSessionPacketHandler<TSessionContext>)
                    handlerFactory.create(handlerType);
            String packetName = handler.packetName();
            if (packetName == null || packetName.isBlank()
                || !packetName.equals(packetName.trim())) {
                throw new ZLinkConfigurationException(
                    "session packet handler must declare a non-empty packet name: "
                        + handlerType.getName());
            }
            if (map.putIfAbsent(packetName, handler) != null) {
                throw new ZLinkConfigurationException(
                    "duplicate session packet handler '" + packetName
                        + "' for stream node");
            }
        }
        return Map.copyOf(map);
    }
}
