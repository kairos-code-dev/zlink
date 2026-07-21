package systems.zlink.framework.runtime.internal.backend;

import java.util.List;
import java.util.function.Consumer;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.channels.ZLinkSubmitStatus;

public interface ZLinkMeshApplicationReceiver extends Consumer<ZLinkMeshDispatchRecord> {
    default void setLocalNodeReadyHandler(Runnable handler) {
    }

    ZLinkSubmitStatus submitLocalNodeSend(
        RoutingId sourceNodeRid,
        byte[] metadata,
        List<Message> parts);
}
