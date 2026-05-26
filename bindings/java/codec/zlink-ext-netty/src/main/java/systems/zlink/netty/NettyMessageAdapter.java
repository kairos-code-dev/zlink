package systems.zlink.netty;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import systems.zlink.contracts.Message;
import io.netty.buffer.ByteBuf;
import java.nio.ByteBuffer;
import java.util.Objects;

final class NettyMessageAdapter {

    private NettyMessageAdapter() {}

    /** Copies the readable bytes from the {@code ByteBuf} without advancing it. */
    public static Message from(ByteBuf source) {
        Objects.requireNonNull(source, "source");
        int length = source.readableBytes();
        if (length == 0)
            return Message.from(new byte[0]);
        int readerIndex = source.readerIndex();
        try {
            ByteBuffer nio = source.nioBufferCount() == 1
                ? source.internalNioBuffer(readerIndex, length)
                : source.nioBuffer(readerIndex, length);
            return Message.from(nio);
        } catch (UnsupportedOperationException ex) {
            byte[] tmp = new byte[length];
            source.getBytes(readerIndex, tmp);
            return Message.from(tmp);
        }
    }

    /** Copies the message data into the writable region of {@code destination}. */
    public static int copyTo(Message message, ByteBuf destination) {
        Objects.requireNonNull(message, "message");
        Objects.requireNonNull(destination, "destination");
        int size = message.size();
        if (destination.writableBytes() < size)
            throw new IllegalArgumentException("destination buffer too small");
        if (size == 0)
            return 0;
        destination.writeBytes(message.dataBuffer());
        return size;
    }
}
