package systems.zlink.framework.runtime.handlers;

import java.lang.reflect.Method;
import java.util.Set;

public record ZLinkScannedHandler(
    ZLinkScannedHandlerSurface surface,
    ZLinkScannedHandlerKind kind,
    Class<?> handlerType,
    Method handlerMethod,
    Class<?> messageType,
    Class<?> replyType,
    String packetName,
    Set<String> groups) {
    public ZLinkScannedHandler(
        ZLinkScannedHandlerSurface surface,
        ZLinkScannedHandlerKind kind,
        Class<?> handlerType,
        Class<?> messageType,
        Class<?> replyType,
        String packetName,
        Set<String> groups) {
        this(surface, kind, handlerType, null, messageType, replyType, packetName, groups);
    }
}
