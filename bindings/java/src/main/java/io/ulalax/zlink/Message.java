/* SPDX-License-Identifier: MPL-2.0 */

package io.ulalax.zlink;

import io.ulalax.zlink.internal.NativeMsg;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;

public final class Message implements AutoCloseable {
    private final Arena arena;
    private final MemorySegment msg;
    private boolean valid;

    public Message() {
        this.arena = Arena.ofConfined();
        this.msg = arena.allocate(64);
        int rc = NativeMsg.msgInit(msg);
        if (rc != 0)
            throw new RuntimeException("zlink_msg_init failed");
        valid = true;
    }

    public Message(int size) {
        this.arena = Arena.ofConfined();
        this.msg = arena.allocate(64);
        int rc = NativeMsg.msgInitSize(msg, size);
        if (rc != 0)
            throw new RuntimeException("zlink_msg_init_size failed");
        valid = true;
    }

    public static Message fromBytes(byte[] data) {
        Message msg = new Message(data.length);
        if (data.length > 0) {
            MemorySegment dst = NativeMsg.msgData(msg.msg).reinterpret(data.length);
            MemorySegment.copy(MemorySegment.ofArray(data), 0, dst, 0, data.length);
        }
        return msg;
    }

    public void send(Socket socket, int flags) {
        int rc = NativeMsg.msgSend(msg, socket.handle(), flags);
        if (rc != 0)
            throw new RuntimeException("zlink_msg_send failed");
        valid = false;
    }

    public void recv(Socket socket, int flags) {
        int rc = NativeMsg.msgRecv(msg, socket.handle(), flags);
        if (rc != 0)
            throw new RuntimeException("zlink_msg_recv failed");
        valid = true;
    }

    public int size() {
        return (int) NativeMsg.msgSize(msg);
    }

    public byte[] data() {
        int size = size();
        if (size <= 0)
            return new byte[0];
        MemorySegment data = NativeMsg.msgData(msg).reinterpret(size);
        byte[] out = new byte[size];
        MemorySegment.copy(data, 0, MemorySegment.ofArray(out), 0, size);
        return out;
    }

    MemorySegment handle() {
        return msg;
    }

    @Override
    public void close() {
        if (valid) {
            NativeMsg.msgClose(msg);
            valid = false;
        }
        arena.close();
    }
}
