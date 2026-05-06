/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;

public final class Poller implements AutoCloseable {
    private static final long POLLER_EVENT_SIZE = 48;
    private static final long EVENT_SOCKET_OFFSET = 8;
    private static final long EVENT_FD_OFFSET = 16;
    private static final long EVENT_TIMER_OFFSET = 24;
    private static final long EVENT_USER_DATA_OFFSET = 32;
    private static final long EVENT_EVENTS_OFFSET = 40;

    private final List<PollItem> items = new ArrayList<>();
    private final Arena eventArena = Arena.ofAuto();
    private Arena tagArena = Arena.ofShared();
    private final Map<Long, PollItem> itemsByToken = new HashMap<>();
    private MemorySegment nativeEvents = MemorySegment.NULL;
    private int nativeEventsCapacity = 0;
    private int lastReadyCount = 0;
    private PollItem[] readyItemsCache = new PollItem[0];
    private short[] readyReventsCache = new short[0];
    private int[] readyFdCache = new int[0];
    private long nextToken = 1L;
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
        PollItem item = newPollItem(socket, socket.handle(), 0, events, tag,
          true);
        int rc = Native.pollerAdd(handle, socket.handle(), item.userData,
          events);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_poller_add");
        registerItem(item);
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
        PollItem item = newPollItem(null, MemorySegment.NULL, fd, events, tag,
          false);
        int rc = Native.pollerAddFd(handle, fd, item.userData, events);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_poller_add_fd");
        registerItem(item);
    }

    public void addFd(int fd, PollEventType... events) {
        addFd(fd, PollEventType.combine(events), null);
    }

    public void addFd(int fd, Object tag, PollEventType... events) {
        addFd(fd, PollEventType.combine(events), tag);
    }

    public void add(Timer timer) {
        add(timer, null);
    }

    public void add(Timer timer, Object tag) {
        ensureOpen();
        Objects.requireNonNull(timer, "timer");
        MemorySegment timerHandle = timer.handle();
        PollItem item = newPollItem(null, MemorySegment.NULL, timer,
          timerHandle, 0, 0, tag, false, true);
        int rc = Native.pollerAddTimer(handle, timerHandle, item.userData);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_poller_add_timer");
        registerItem(item);
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
        unregisterItem(index);
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
        unregisterItem(index);
        return true;
    }

    public boolean remove(Timer timer) {
        ensureOpen();
        Objects.requireNonNull(timer, "timer");
        int index = findTimer(timer.handle());
        if (index < 0)
            return false;
        int rc = Native.pollerRemoveTimer(handle, timer.handle());
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_poller_remove_timer");
        unregisterItem(index);
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
        closeArena(tagArena);
        tagArena = Arena.ofShared();
        items.clear();
        itemsByToken.clear();
        nativeEvents = MemorySegment.NULL;
        nativeEventsCapacity = 0;
        lastReadyCount = 0;
        nextToken = 1L;
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
        if (items.isEmpty()) {
            lastReadyCount = 0;
            return 0;
        }
        MemorySegment events = ensureNativeEvents(items.size());
        int readyCount = Native.pollerWaitAll(handle, events, items.size(),
          timeoutMs);
        if (readyCount < 0)
            throw ZlinkException.fromLastError("zlink_poller_wait_all");
        ensureReadyCacheCapacity(readyCount);
        for (int i = 0; i < readyCount; i++) {
            long base = (long) i * POLLER_EVENT_SIZE;
            readyReventsCache[i] = events.get(ValueLayout.JAVA_SHORT,
              base + EVENT_EVENTS_OFFSET);
            readyFdCache[i] = events.get(ValueLayout.JAVA_INT,
              base + EVENT_FD_OFFSET);
            readyItemsCache[i] = resolveReadyItem(events, base);
        }
        lastReadyCount = readyCount;
        return readyCount;
    }

    public boolean pollAny(int timeoutMs) {
        return pollCount(timeoutMs) > 0;
    }

    public List<PollEvent> poll(int timeoutMs) {
        int readyCount = pollCount(timeoutMs);
        if (readyCount == 0)
            return List.of();

        List<PollEvent> out = new ArrayList<>(readyCount);
        for (int i = 0; i < readyCount; i++) {
            PollItem item = cachedReadyItem(i);
            int fd = readyFd(i);
            short revents = readyRevents(i);
            out.add(new PollEvent(item == null ? null : item.socket, revents,
              fd, item == null ? null : item.tag,
              item == null ? revents : item.events));
        }
        return out;
    }

    public int readyCount() {
        ensureOpen();
        return lastReadyCount;
    }

    public Socket readySocket(int index) {
        PollItem item = cachedReadyItem(index);
        return item == null ? null : item.socket;
    }

    public Timer readyTimer(int index) {
        PollItem item = cachedReadyItem(index);
        return item == null ? null : item.timer;
    }

    public Object readyTag(int index) {
        PollItem item = cachedReadyItem(index);
        return item == null ? null : item.tag;
    }

    public int readyFd(int index) {
        checkReadyIndex(index);
        return readyFdCache[index];
    }

    public int readyEvents(int index) {
        PollItem item = cachedReadyItem(index);
        return item == null ? readyRevents(index) : item.events;
    }

    public short readyRevents(int index) {
        checkReadyIndex(index);
        return readyReventsCache[index];
    }

    @Override
    public void close() {
        if (handle == null || handle.address() == 0)
            return;
        Native.pollerDestroy(handle);
        handle = MemorySegment.NULL;
        items.clear();
        itemsByToken.clear();
        nativeEvents = MemorySegment.NULL;
        nativeEventsCapacity = 0;
        lastReadyCount = 0;
        readyItemsCache = new PollItem[0];
        readyReventsCache = new short[0];
        readyFdCache = new int[0];
        closeArena(tagArena);
        tagArena = null;
    }

    private void ensureOpen() {
        if (handle == null || handle.address() == 0)
            throw new IllegalStateException("poller is closed");
    }

    private MemorySegment ensureNativeEvents(int requiredCount) {
        if (nativeEventsCapacity < requiredCount) {
            nativeEvents = eventArena.allocate(POLLER_EVENT_SIZE * requiredCount,
              ValueLayout.ADDRESS.byteAlignment());
            nativeEventsCapacity = requiredCount;
        }
        return nativeEvents;
    }

    private PollItem cachedReadyItem(int index) {
        checkReadyIndex(index);
        return readyItemsCache[index];
    }

    private PollItem resolveReadyItem(MemorySegment events, long base) {
        MemorySegment userData = events.get(ValueLayout.ADDRESS,
          base + EVENT_USER_DATA_OFFSET);
        if (userData.address() != 0) {
            PollItem item = itemForUserData(userData);
            if (item != null)
                return item;
        }
        MemorySegment socketHandle = events.get(ValueLayout.ADDRESS,
          base + EVENT_SOCKET_OFFSET);
        int fd = events.get(ValueLayout.JAVA_INT, base + EVENT_FD_OFFSET);
        if (socketHandle.address() != 0)
            return findSocketItem(socketHandle);
        MemorySegment timerHandle = events.get(ValueLayout.ADDRESS,
          base + EVENT_TIMER_OFFSET);
        if (timerHandle.address() != 0)
            return findTimerItem(timerHandle);
        return findFdItem(fd);
    }

    private void ensureReadyCacheCapacity(int requiredCount) {
        if (readyItemsCache.length < requiredCount) {
            readyItemsCache = new PollItem[requiredCount];
            readyReventsCache = new short[requiredCount];
            readyFdCache = new int[requiredCount];
        }
    }

    private void checkReadyIndex(int index) {
        if (index < 0 || index >= lastReadyCount)
            throw new IndexOutOfBoundsException("ready index " + index);
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
            if (!item.isSocket && !item.isTimer && item.fd == fd)
                return i;
        }
        return -1;
    }

    private int findTimer(MemorySegment timerHandle) {
        for (int i = 0; i < items.size(); i++) {
            PollItem item = items.get(i);
            if (item.isTimer && item.timerHandle.address()
              == timerHandle.address()) {
                return i;
            }
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

    private PollItem findTimerItem(MemorySegment timerHandle) {
        int index = findTimer(timerHandle);
        return index >= 0 ? items.get(index) : null;
    }

    private PollItem newPollItem(Socket socket, MemorySegment socketHandle,
                                 int fd, int events, Object tag,
                                 boolean isSocket) {
        return newPollItem(socket, socketHandle, null, MemorySegment.NULL, fd,
          events, tag, isSocket, false);
    }

    private PollItem newPollItem(Socket socket, MemorySegment socketHandle,
                                 Timer timer, MemorySegment timerHandle,
                                 int fd, int events, Object tag,
                                 boolean isSocket, boolean isTimer) {
        long token = nextToken++;
        MemorySegment userData = tagArena.allocate(ValueLayout.JAVA_LONG);
        userData.set(ValueLayout.JAVA_LONG, 0, token);
        return new PollItem(socket, socketHandle, timer, timerHandle, fd,
          events, tag, isSocket, isTimer, token, userData);
    }

    private void registerItem(PollItem item) {
        items.add(item);
        itemsByToken.put(item.token, item);
    }

    private void unregisterItem(int index) {
        PollItem item = items.remove(index);
        itemsByToken.remove(item.token);
    }

    private PollItem itemForUserData(MemorySegment userData) {
        MemorySegment tokenSegment = MemorySegment.ofAddress(userData.address())
          .reinterpret(ValueLayout.JAVA_LONG.byteSize());
        long token = tokenSegment.get(ValueLayout.JAVA_LONG, 0);
        return itemsByToken.get(token);
    }

    private static void closeArena(Arena arena) {
        if (arena != null && arena.scope().isAlive())
            arena.close();
    }

    private static final class PollItem {
        public final Socket socket;
        public final MemorySegment socketHandle;
        public final Timer timer;
        public final MemorySegment timerHandle;
        public final int fd;
        public int events;
        public final Object tag;
        public final boolean isSocket;
        public final boolean isTimer;
        public final long token;
        public final MemorySegment userData;

        PollItem(Socket socket, MemorySegment socketHandle, Timer timer,
                 MemorySegment timerHandle, int fd, int events,
                 Object tag, boolean isSocket, boolean isTimer, long token,
                 MemorySegment userData) {
            this.socket = socket;
            this.socketHandle = socketHandle;
            this.timer = timer;
            this.timerHandle = timerHandle;
            this.fd = fd;
            this.events = events;
            this.tag = tag;
            this.isSocket = isSocket;
            this.isTimer = isTimer;
            this.token = token;
            this.userData = userData;
        }
    }

}
