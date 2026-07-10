package systems.zlink.framework.runtime.binding;

import systems.zlink.contracts.sockets.Socket;

interface ZLinkJavaSocketBacked {
    Socket nativeSocket();
}
