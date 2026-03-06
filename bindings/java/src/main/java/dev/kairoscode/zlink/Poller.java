/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

public final class Poller implements AutoCloseable {
    private static final long POLLER_EVENT_SIZE = 32;
    private static final long EVENT_SOCKET_OFFSET = 0;
    private static final long EVENT_FD_OFFSET = 8;
    private static final long EVENT_EVENTS_OFFSET = 24;

    private final List<PollItem> items = new ArrayList<>();
    private final Arena eventArena = Arena.ofAuto();
    private MemorySegment nativeEvents = MemorySegment.NULL;
    private int nativeEventsCapacity = 0;
    private MemorySegment handle;

    public Poller() {
        handle = Native.pollerNew();
        if (handle == null || handle.address() == 0)
            throw ZlinkException.fromLastError("zlink_poller_new");
    }

    public void add(Socket socket, int events) {
        add(socket, events, null);
    }

    public void add(Socket socket, int events, Object tag) {
        ensureOpen();
        Objects.requireNonNull(socket, "socket");
        int rc = Native.pollerAdd(handle, socket.handle(), MemorySegment.NULL,
            events);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_poller_add");
        items.add(new PollItem(socket, socket.handle(), 0, events, tag, true));
    }

    public void add(Socket socket, PollEventType... events) {
        add(socket, PollEventType.combine(events), null);
    }

    public void add(Socket socket, Object tag, PollEventType... events) {
        add(socket, PollEventType.combine(events), tag);
    }

    public void addFd(int fd, int events) {
        addFd(fd, events, null);
    }

    public void addFd(int fd, int events, Object tag) {
        ensureOpen();
        int rc = Native.pollerAddFd(handle, fd, MemorySegment.NULL, events);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_poller_add_fd");
        items.add(new PollItem(null, MemorySegment.NULL, fd, events, tag,
            false));
    }

    public void addFd(int fd, PollEventType... events) {
        addFd(fd, PollEventType.combine(events), null);
    }

    public void addFd(int fd, Object tag, PollEventType... events) {
        addFd(fd, PollEventType.combine(events), tag);
    }

    public void modify(Socket socket, int events) {
        ensureOpen();
        Objects.requireNonNull(socket, "socket");
        int index = findSocket(socket.handle());
        if (index < 0)
            throw new IllegalArgumentException("socket is not registered");
        int rc = Native.pollerModify(handle, socket.handle(), events);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_poller_modify");
        items.get(index).events = events;
    }

    public void modify(Socket socket, PollEventType... events) {
        modify(socket, PollEventType.combine(events));
    }

    public void modifyFd(int fd, int events) {
        ensureOpen();
        int index = findFd(fd);
        if (index < 0)
            throw new IllegalArgumentException("fd is not registered");
        int rc = Native.pollerModifyFd(handle, fd, events);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_poller_modify_fd");
        items.get(index).events = events;
    }

    public void modifyFd(int fd, PollEventType... events) {
        modifyFd(fd, PollEventType.combine(events));
    }

    public boolean remove(Socket socket) {
        ensureOpen();
        Objects.requireNonNull(socket, "socket");
        int index = findSocket(socket.handle());
        if (index < 0)
            return false;
        int rc = Native.pollerRemove(handle, socket.handle());
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_poller_remove");
        items.remove(index);
        return true;
    }

    public boolean removeFd(int fd) {
        ensureOpen();
        int index = findFd(fd);
        if (index < 0)
            return false;
        int rc = Native.pollerRemoveFd(handle, fd);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_poller_remove_fd");
        items.remove(index);
        return true;
    }

    public void clear() {
        ensureOpen();
        int rc = Native.pollerDestroy(handle);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_poller_destroy");
        handle = Native.pollerNew();
        if (handle == null || handle.address() == 0)
            throw ZlinkException.fromLastError("zlink_poller_new");
        items.clear();
        nativeEvents = MemorySegment.NULL;
        nativeEventsCapacity = 0;
    }

    public int size() {
        ensureOpen();
        int rc = Native.pollerSize(handle);
        if (rc < 0)
            throw ZlinkException.fromLastError("zlink_poller_size");
        return rc;
    }

    public int pollCount(int timeoutMs) {
        ensureOpen();
        if (items.isEmpty())
            return 0;
        MemorySegment arr = ensureNativeEvents(items.size());
        int rc = Native.pollerWaitAll(handle, arr, items.size(), timeoutMs);
        if (rc < 0)
            throw ZlinkException.fromLastError("zlink_poller_wait_all");
        return rc;
    }

    public boolean pollAny(int timeoutMs) {
        return pollCount(timeoutMs) > 0;
    }

    public List<PollEvent> poll(int timeoutMs) {
        ensureOpen();
        if (items.isEmpty())
            return List.of();

        MemorySegment arr = ensureNativeEvents(items.size());
        int readyCount = Native.pollerWaitAll(handle, arr, items.size(),
            timeoutMs);
        if (readyCount < 0)
            throw ZlinkException.fromLastError("zlink_poller_wait_all");
        if (readyCount == 0)
            return List.of();

        List<PollEvent> out = new ArrayList<>(readyCount);
        for (int i = 0; i < readyCount; i++) {
            long base = (long) i * POLLER_EVENT_SIZE;
            MemorySegment socketHandle = arr.get(ValueLayout.ADDRESS,
                base + EVENT_SOCKET_OFFSET);
            int fd = arr.get(ValueLayout.JAVA_INT, base + EVENT_FD_OFFSET);
            short revents = arr.get(ValueLayout.JAVA_SHORT,
                base + EVENT_EVENTS_OFFSET);

            PollItem item = socketHandle.address() != 0
                ? findSocketItem(socketHandle) : findFdItem(fd);
            out.add(new PollEvent(item == null ? null : item.socket, revents,
                fd, item == null ? null : item.tag,
                item == null ? revents : item.events));
        }
        return out;
    }

    @Override
    public void close() {
        if (handle == null || handle.address() == 0)
            return;
        Native.pollerDestroy(handle);
        handle = MemorySegment.NULL;
        items.clear();
        nativeEvents = MemorySegment.NULL;
        nativeEventsCapacity = 0;
    }

    private void ensureOpen() {
        if (handle == null || handle.address() == 0)
            throw new IllegalStateException("poller is closed");
    }

    private MemorySegment ensureNativeEvents(int requiredCount) {
        if (nativeEventsCapacity < requiredCount) {
            nativeEvents = eventArena.allocate(POLLER_EVENT_SIZE
                * requiredCount, ValueLayout.ADDRESS.byteAlignment());
            nativeEventsCapacity = requiredCount;
        }
        return nativeEvents;
    }

    private int findSocket(MemorySegment socketHandle) {
        for (int i = 0; i < items.size(); i++) {
            PollItem item = items.get(i);
            if (item.isSocket && item.socketHandle.address()
                == socketHandle.address()) {
                return i;
            }
        }
        return -1;
    }

    private int findFd(int fd) {
        for (int i = 0; i < items.size(); i++) {
            PollItem item = items.get(i);
            if (!item.isSocket && item.fd == fd)
                return i;
        }
        return -1;
    }

    private PollItem findSocketItem(MemorySegment socketHandle) {
        int index = findSocket(socketHandle);
        return index >= 0 ? items.get(index) : null;
    }

    private PollItem findFdItem(int fd) {
        int index = findFd(fd);
        return index >= 0 ? items.get(index) : null;
    }

    public static final class PollItem {
        public final Socket socket;
        public final MemorySegment socketHandle;
        public final int fd;
        public int events;
        public final Object tag;
        public final boolean isSocket;

        PollItem(Socket socket, MemorySegment socketHandle, int fd, int events,
                 Object tag, boolean isSocket) {
            this.socket = socket;
            this.socketHandle = socketHandle;
            this.fd = fd;
            this.events = events;
            this.tag = tag;
            this.isSocket = isSocket;
        }
    }

    public record PollEvent(Socket socket, int revents, int fd, Object tag,
                            int events) {
        public PollEvent(Socket socket, int revents) {
            this(socket, revents, 0, null, 0);
        }
    }
}
