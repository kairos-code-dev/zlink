/* SPDX-License-Identifier: MPL-2.0 */

package io.ulalax.zlink;

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

    static ByteBuf wrapNetty(Object nettyByteBuf) {
        return new NettyByteBuf(nettyByteBuf);
    }
}
