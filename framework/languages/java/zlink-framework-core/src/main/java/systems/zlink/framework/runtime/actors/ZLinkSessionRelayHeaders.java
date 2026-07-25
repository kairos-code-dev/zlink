package systems.zlink.framework.runtime.actors;

import java.util.Collections;
import java.util.Map;
import java.util.Optional;
import java.util.WeakHashMap;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeader;
import systems.zlink.framework.streams.ZLinkSessionMessageContext;

final class ZLinkSessionRelayHeaders {
    private final ThreadLocal<ZLinkStreamHeader> current = new ThreadLocal<>();
    private final Map<ZLinkSessionMessageContext, ZLinkStreamHeader> byDispatch =
        Collections.synchronizedMap(new WeakHashMap<>());

    void enter(ZLinkStreamHeader header) {
        current.set(header);
    }

    void enter(
        ZLinkSessionMessageContext dispatch,
        ZLinkStreamHeader header) {
        if (dispatch != null && header != null) {
            byDispatch.put(dispatch, header);
        }
        enter(header);
    }

    void exit() {
        current.remove();
    }

    void exit(ZLinkSessionMessageContext dispatch) {
        if (dispatch != null) {
            byDispatch.remove(dispatch);
        }
        exit();
    }

    Optional<ZLinkStreamHeader> current() {
        return Optional.ofNullable(current.get());
    }

    Optional<ZLinkStreamHeader> find(ZLinkSessionMessageContext dispatch) {
        if (dispatch == null) {
            return Optional.empty();
        }
        return Optional.ofNullable(byDispatch.get(dispatch));
    }
}
