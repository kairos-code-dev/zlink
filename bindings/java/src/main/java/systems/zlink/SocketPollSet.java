/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink;

import systems.zlink.internal.Native;
import java.lang.foreign.Arena;
import java.lang.foreign.MemoryLayout;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.util.List;
import java.util.Objects;

final class SocketPollSet implements AutoCloseable {
    private static final ValueLayout FD_LAYOUT =
        isWindows64() ? ValueLayout.JAVA_LONG : ValueLayout.JAVA_INT;
    private static final MemoryLayout ITEM_LAYOUT = MemoryLayout.structLayout(
        ValueLayout.ADDRESS.withName("socket"),
        FD_LAYOUT.withName("fd"),
        ValueLayout.JAVA_SHORT.withName("events"),
        ValueLayout.JAVA_SHORT.withName("revents"));
    private static final long SOCKET_OFFSET =
        ITEM_LAYOUT.byteOffset(MemoryLayout.PathElement.groupElement("socket"));
    private static final long FD_OFFSET =
        ITEM_LAYOUT.byteOffset(MemoryLayout.PathElement.groupElement("fd"));
    private static final long EVENTS_OFFSET =
        ITEM_LAYOUT.byteOffset(MemoryLayout.PathElement.groupElement("events"));
    private static final long REVENTS_OFFSET =
        ITEM_LAYOUT.byteOffset(MemoryLayout.PathElement.groupElement("revents"));
    private static final long ITEM_SIZE = ITEM_LAYOUT.byteSize();

    private final Arena arena = Arena.ofShared();
    private final MemorySegment items;
    private final Socket[] sockets;
    private final int count;

    SocketPollSet(int count) {
        if (count < 0) {
            throw new IllegalArgumentException("count must be non-negative");
        }
        this.count = count;
        this.items = count == 0 ? MemorySegment.NULL
            : arena.allocate(ITEM_LAYOUT, count);
        this.sockets = new Socket[count];
    }

    static SocketPollSet fromSockets(List<Socket> sockets,
                                     int initialEvents) {
        Objects.requireNonNull(sockets, "sockets");
        SocketPollSet pollSet = new SocketPollSet(sockets.size());
        for (int i = 0; i < sockets.size(); i++) {
            pollSet.setSocket(i, sockets.get(i), initialEvents);
        }
        return pollSet;
    }

    int size() {
        return count;
    }

    void setSocket(int index, Socket socket, int events) {
        Objects.requireNonNull(socket, "socket");
        checkIndex(index);
        sockets[index] = socket;
        long base = itemBase(index);
        items.set(ValueLayout.ADDRESS, base + SOCKET_OFFSET, socket.handle());
        setFdZero(base);
        items.set(ValueLayout.JAVA_SHORT, base + EVENTS_OFFSET, (short) events);
        items.set(ValueLayout.JAVA_SHORT, base + REVENTS_OFFSET, (short) 0);
    }

    Socket socket(int index) {
        checkIndex(index);
        return sockets[index];
    }

    void setEvents(int index, int events) {
        checkIndex(index);
        items.set(ValueLayout.JAVA_SHORT, itemBase(index) + EVENTS_OFFSET,
            (short) events);
    }

    int events(int index) {
        checkIndex(index);
        return items.get(ValueLayout.JAVA_SHORT, itemBase(index) + EVENTS_OFFSET)
            & 0xFFFF;
    }

    int revents(int index) {
        checkIndex(index);
        return items.get(ValueLayout.JAVA_SHORT, itemBase(index) + REVENTS_OFFSET)
            & 0xFFFF;
    }

    boolean isReady(int index, int eventMask) {
        return (revents(index) & eventMask) != 0;
    }

    int poll(int timeoutMs) {
        if (count == 0) {
            return 0;
        }
        clearRevents();
        int rc = Native.pollRaw(items, count, timeoutMs);
        if (rc < 0) {
            throw ZlinkException.fromLastError("zlink_poll");
        }
        return rc;
    }

    @Override
    public void close() {
        if (arena.scope().isAlive()) {
            arena.close();
        }
    }

    private void clearRevents() {
        for (int i = 0; i < count; i++) {
            items.set(ValueLayout.JAVA_SHORT, itemBase(i) + REVENTS_OFFSET,
                (short) 0);
        }
    }

    private void setFdZero(long base) {
        if (FD_LAYOUT == ValueLayout.JAVA_LONG) {
            items.set(ValueLayout.JAVA_LONG, base + FD_OFFSET, 0L);
            return;
        }
        items.set(ValueLayout.JAVA_INT, base + FD_OFFSET, 0);
    }

    private long itemBase(int index) {
        return (long) index * ITEM_SIZE;
    }

    private void checkIndex(int index) {
        if (index < 0 || index >= count) {
            throw new IndexOutOfBoundsException("index " + index);
        }
    }

    private static boolean isWindows64() {
        String os = System.getProperty("os.name", "").toLowerCase();
        String arch = System.getProperty("os.arch", "").toLowerCase();
        return os.contains("win") && (arch.contains("64") || arch.contains("amd64"));
    }
}
