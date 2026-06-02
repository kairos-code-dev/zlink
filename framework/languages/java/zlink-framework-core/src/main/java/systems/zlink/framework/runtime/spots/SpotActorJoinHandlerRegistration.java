package systems.zlink.framework.runtime.spots;

import java.lang.reflect.Method;

record SpotActorJoinHandlerRegistration(
    Class<?> handlerType,
    Method handlerMethod,
    Class<?> actorType,
    Class<?> requestType,
    Class<?> replyType,
    String packetName) {
}
