package consumer;

import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.eventing.MonitorStatus;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.messaging.SendOperation;
import systems.zlink.contracts.sockets.Socket;
import systems.zlink.contracts.sockets.StreamSocket;

final class PublicConsumer {
    public static void main(String[] args) {
        int[] version = Zlink.version();
        if (version.length != 3 || version[0] != 11
                || version[1] != 0 || version[2] != 0) {
            throw new IllegalStateException(
                "Expected packaged Core 11.0.0, found "
                    + java.util.Arrays.toString(version));
        }
        System.out.println("ZLINK_CORE_VERSION=11.0.0");
    }

    static void verify(Context context, Socket socket, StreamSocket stream,
                       MonitorStatus status, SendOperation send) {
        try (var monitor = socket.monitorOpen()) {
            status.isReady();
        }
        stream.onPacket((routingId, header, body) -> {
            header.close();
            body.close();
        });
        try (Message first = Message.from("first");
             Message second = Message.from("second")) {
            send.message(first).message(second);
        }
        context.shutdown();
    }

    private PublicConsumer() {
    }
}
