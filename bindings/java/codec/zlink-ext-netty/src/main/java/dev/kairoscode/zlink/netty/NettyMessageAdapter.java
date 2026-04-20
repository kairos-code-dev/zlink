package dev.kairoscode.zlink.netty;

import dev.kairoscode.zlink.Message;
import io.netty.buffer.ByteBuf;
import java.nio.ByteBuffer;
import java.util.Objects;

public final class NettyMessageAdapter {

    private NettyMessageAdapter() {}

    /** Copies the readable bytes from the {@code ByteBuf} without advancing it. */
    public static Message copyOf(ByteBuf source) {
        Objects.requireNonNull(source, "source");
        int length = source.readableBytes();
        if (length == 0)
            return Message.copyOf(new byte[0]);
        int readerIndex = source.readerIndex();
        try {
            ByteBuffer nio = source.nioBufferCount() == 1
                ? source.internalNioBuffer(readerIndex, length)
                : source.nioBuffer(readerIndex, length);
            return Message.copyOf(nio);
        } catch (UnsupportedOperationException ex) {
            byte[] tmp = new byte[length];
            source.getBytes(readerIndex, tmp);
            return Message.copyOf(tmp);
        }
    }

    /**
     * Borrows the readable bytes from a direct {@code ByteBuf} without copying.
     * The caller must keep {@code source} valid until the returned message is closed.
     * Falls back to a copy for non-direct backing.
     */
    public static Message wrap(ByteBuf source) {
        Objects.requireNonNull(source, "source");
        int length = source.readableBytes();
        if (length == 0)
            return Message.copyOf(new byte[0]);
        int readerIndex = source.readerIndex();
        ByteBuffer nio = source.nioBuffer(readerIndex, length);
        if (nio.isDirect())
            return Message.wrapDirect(nio);
        return Message.copyOf(nio);
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
