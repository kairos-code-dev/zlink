package dev.kairoscode.zlink.netty;

import dev.kairoscode.zlink.Message;
import io.netty.buffer.ByteBuf;

public final class NettyMessages {
    private NettyMessages() {}

    public static Message copyOf(ByteBuf source) {
        return NettyMessageAdapter.copyOf(source);
    }

    public static int copyTo(Message message, ByteBuf destination) {
        return NettyMessageAdapter.copyTo(message, destination);
    }
}
