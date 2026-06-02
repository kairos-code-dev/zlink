package systems.zlink.samples.bingo.client;

import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class BingoNotificationLoopbackServer implements AutoCloseable {
    private final ServerSocket server;
    private final ExecutorService executor = Executors.newCachedThreadPool(runnable -> {
        Thread thread = new Thread(runnable, "bingo-notification-loopback");
        thread.setDaemon(true);
        return thread;
    });

    public BingoNotificationLoopbackServer(int port) throws IOException {
        this.server = new ServerSocket(port);
        executor.execute(this::acceptLoop);
    }

    private void acceptLoop() {
        while (!server.isClosed()) {
            try {
                Socket socket = server.accept();
                executor.execute(() -> serve(socket));
            } catch (IOException ex) {
                if (!server.isClosed()) {
                    throw new IllegalStateException("notification accept failed", ex);
                }
            }
        }
    }

    private void serve(Socket socket) {
        try (socket) {
            DataInputStream input = new DataInputStream(socket.getInputStream());
            DataOutputStream output = new DataOutputStream(socket.getOutputStream());
            while (!socket.isClosed()) {
                int headerLength = input.readUnsignedShort();
                int payloadLength = input.readInt();
                byte[] header = input.readNBytes(headerLength);
                byte[] payload = input.readNBytes(payloadLength);
                String packetName = decodePacketName(header);
                output.write(encodeSendFrame(packetName, payload));
                output.flush();
            }
        } catch (IOException ignored) {
        }
    }

    private static String decodePacketName(byte[] header) {
        ByteBuffer buffer = ByteBuffer.wrap(header);
        buffer.get();
        buffer.get();
        int flags = Byte.toUnsignedInt(buffer.get());
        if ((flags & 0x01) != 0) {
            buffer.getLong();
        }
        int nameLength = Byte.toUnsignedInt(buffer.get());
        byte[] name = new byte[nameLength];
        buffer.get(name);
        return new String(name, StandardCharsets.UTF_8);
    }

    private static byte[] encodeSendFrame(String packetName, byte[] payload) {
        byte[] name = packetName.getBytes(StandardCharsets.UTF_8);
        byte[] header = ByteBuffer.allocate(3 + 1 + name.length)
            .put((byte) 1)
            .put((byte) 0)
            .put((byte) 0)
            .put((byte) name.length)
            .put(name)
            .array();
        return ByteBuffer.allocate(6 + header.length + payload.length)
            .putShort((short) header.length)
            .putInt(payload.length)
            .put(header)
            .put(payload)
            .array();
    }

    @Override
    public void close() throws Exception {
        server.close();
        executor.shutdownNow();
    }
}
