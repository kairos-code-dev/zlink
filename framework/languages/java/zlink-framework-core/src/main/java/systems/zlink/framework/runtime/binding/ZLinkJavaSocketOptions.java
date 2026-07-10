package systems.zlink.framework.runtime.binding;

import java.time.Duration;
import systems.zlink.contracts.sockets.Socket;

final class ZLinkJavaSocketOptions {
    private ZLinkJavaSocketOptions() {
    }

    static <T extends Socket> T configureFrameworkSocket(T socket) {
        socket.options().linger(Duration.ZERO);
        return socket;
    }
}
