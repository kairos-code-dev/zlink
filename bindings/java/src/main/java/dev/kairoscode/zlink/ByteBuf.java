/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import java.nio.ByteBuffer;

public interface ByteBuf {
    ByteBuffer nioBuffer();

    int readableBytes();

    int writableBytes();

    int readerIndex();

    int writerIndex();

    void setReaderIndex(int index);

    void setWriterIndex(int index);

    void advanceReader(int bytes);

    void advanceWriter(int bytes);

    static ByteBuf wrap(ByteBuffer buffer) {
        return new NioByteBuf(buffer);
    }

    static ByteBuf wrapNetty(io.netty.buffer.ByteBuf nettyByteBuf) {
        return new NettyByteBuf(nettyByteBuf);
    }

    @Deprecated
    static ByteBuf wrapNetty(Object nettyByteBuf) {
        if (nettyByteBuf instanceof io.netty.buffer.ByteBuf nettyBuf)
            return new NettyByteBuf(nettyBuf);
        throw new IllegalArgumentException(
            "Expected io.netty.buffer.ByteBuf but got: "
              + (nettyByteBuf == null ? "null"
              : nettyByteBuf.getClass().getName()));
    }
}
