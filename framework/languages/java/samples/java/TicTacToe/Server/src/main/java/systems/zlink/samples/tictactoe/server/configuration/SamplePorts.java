package systems.zlink.samples.tictactoe.server.configuration;

import java.io.IOException;
import java.net.ServerSocket;

public final class SamplePorts {
    private SamplePorts() {
    }

    public static int reserve() {
        try (ServerSocket socket = new ServerSocket(0)) {
            socket.setReuseAddress(true);
            return socket.getLocalPort();
        } catch (IOException ex) {
            throw new IllegalStateException("Failed to reserve a sample port.", ex);
        }
    }
}
