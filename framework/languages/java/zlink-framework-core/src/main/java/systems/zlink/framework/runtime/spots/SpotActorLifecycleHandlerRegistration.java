package systems.zlink.framework.runtime.spots;

import java.lang.reflect.Method;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandlerKind;

record SpotActorLifecycleHandlerRegistration(
    Class<?> handlerType,
    Method handlerMethod,
    Class<?> spotType,
    Class<?> actorType,
    ZLinkScannedHandlerKind kind) {
}
