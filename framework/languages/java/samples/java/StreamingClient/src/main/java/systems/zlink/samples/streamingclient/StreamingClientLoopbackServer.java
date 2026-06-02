package systems.zlink.samples.streamingclient;

import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class StreamingClientLoopbackServer implements AutoCloseable {
    private final ServerSocket server;
    private final ExecutorService executor = Executors.newCachedThreadPool(runnable -> {
        Thread thread = new Thread(runnable, "streaming-client-loopback");
        thread.setDaemon(true);
        return thread;
    });

    public StreamingClientLoopbackServer(int port) throws IOException {
        server = new ServerSocket(port);
        executor.execute(this::acceptLoop);
    }

    private void acceptLoop() {
        while (!server.isClosed()) {
            try {
                Socket socket = server.accept();
                executor.execute(() -> serve(socket));
            } catch (IOException ex) {
                if (!server.isClosed()) {
                    throw new IllegalStateException("loopback accept failed", ex);
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
                Header header = decodeHeader(input.readNBytes(headerLength));
                byte[] payload = input.readNBytes(payloadLength);
                writeFrame(output, encodeHeader(1, null, header.name(), header.metadata()), payload);
                if (header.kind() == 2 && header.requestSeq() != null) {
                    writeFrame(output, encodeHeader(
                        3,
                        header.requestSeq(),
                        header.name(),
                        header.metadata()), payload);
                }
            }
        } catch (IOException ignored) {
        }
    }

    private static void writeFrame(DataOutputStream output, byte[] header, byte[] payload)
        throws IOException {
        output.write(ByteBuffer.allocate(6 + header.length + payload.length)
            .putShort((short) header.length)
            .putInt(payload.length)
            .put(header)
            .put(payload)
            .array());
        output.flush();
    }

    private static Header decodeHeader(byte[] header) {
        ByteBuffer buffer = ByteBuffer.wrap(header);
        int kind = Byte.toUnsignedInt(buffer.get());
        buffer.get();
        int flags = Byte.toUnsignedInt(buffer.get());
        Long requestSeq = (flags & 0x01) == 0 ? null : buffer.getLong();
        int nameLength = Byte.toUnsignedInt(buffer.get());
        byte[] name = new byte[nameLength];
        buffer.get(name);
        Map<String, String> metadata = new LinkedHashMap<>();
        if ((flags & 0x02) != 0) {
            int metadataLength = Short.toUnsignedInt(buffer.getShort());
            ByteBuffer metadataBuffer = ByteBuffer.wrap(readBytes(buffer, metadataLength));
            int count = Byte.toUnsignedInt(metadataBuffer.get());
            for (int i = 0; i < count; i++) {
                int keyLength = Byte.toUnsignedInt(metadataBuffer.get());
                String key = new String(readBytes(metadataBuffer, keyLength), StandardCharsets.UTF_8);
                int valueLength = Short.toUnsignedInt(metadataBuffer.getShort());
                String value = new String(readBytes(metadataBuffer, valueLength), StandardCharsets.UTF_8);
                metadata.put(key, value);
            }
        }
        return new Header(kind, requestSeq, new String(name, StandardCharsets.UTF_8), metadata);
    }

    private static byte[] encodeHeader(
        int kind,
        Long requestSeq,
        String packetName,
        Map<String, String> metadata) {
        byte[] name = packetName.getBytes(StandardCharsets.UTF_8);
        byte[] metadataBytes = encodeMetadata(metadata);
        int flags = (requestSeq == null ? 0 : 0x01) | (metadataBytes.length == 0 ? 0 : 0x02);
        ByteBuffer buffer = ByteBuffer.allocate(
            3
                + (requestSeq == null ? 0 : 8)
                + 1
                + name.length
                + (metadataBytes.length == 0 ? 0 : 2 + metadataBytes.length));
        buffer.put((byte) kind);
        buffer.put((byte) 0);
        buffer.put((byte) flags);
        if (requestSeq != null) {
            buffer.putLong(requestSeq);
        }
        buffer.put((byte) name.length);
        buffer.put(name);
        if (metadataBytes.length > 0) {
            buffer.putShort((short) metadataBytes.length);
            buffer.put(metadataBytes);
        }
        return buffer.array();
    }

    private static byte[] encodeMetadata(Map<String, String> metadata) {
        if (metadata.isEmpty()) {
            return new byte[0];
        }
        int size = 1;
        for (Map.Entry<String, String> entry : metadata.entrySet()) {
            size += 1
                + entry.getKey().getBytes(StandardCharsets.UTF_8).length
                + 2
                + entry.getValue().getBytes(StandardCharsets.UTF_8).length;
        }
        ByteBuffer buffer = ByteBuffer.allocate(size);
        buffer.put((byte) metadata.size());
        for (Map.Entry<String, String> entry : metadata.entrySet()) {
            byte[] key = entry.getKey().getBytes(StandardCharsets.UTF_8);
            byte[] value = entry.getValue().getBytes(StandardCharsets.UTF_8);
            buffer.put((byte) key.length);
            buffer.put(key);
            buffer.putShort((short) value.length);
            buffer.put(value);
        }
        return buffer.array();
    }

    private static byte[] readBytes(ByteBuffer buffer, int length) {
        byte[] bytes = new byte[length];
        buffer.get(bytes);
        return bytes;
    }

    @Override
    public void close() throws Exception {
        server.close();
        executor.shutdownNow();
    }

    private record Header(
        int kind,
        Long requestSeq,
        String name,
        Map<String, String> metadata) {
    }
}
