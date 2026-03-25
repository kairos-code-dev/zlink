/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.service.receiver.Receiver;
import dev.kairoscode.zlink.service.spot.Spot;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;

public final class Poller implements AutoCloseable {
    private static final long POLLER_EVENT_SIZE = 32;
    private static final long EVENT_SOCKET_OFFSET = 0;
    private static final long EVENT_FD_OFFSET = 8;
    private static final long EVENT_USER_DATA_OFFSET = 16;
    private static final long EVENT_EVENTS_OFFSET = 24;

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

    public void addSpotSub(Spot spot, int events) {
        addSpotSub(spot, events, null);
    }

    public void addSpotSub(Spot spot, int events, Object tag) {
        ensureOpen();
        Objects.requireNonNull(spot, "spot");
        MemorySegment spotSub = spot.subHandle();
        PollItem item = newPollItem(null, spotSub, 0, events, tag, true);
        int rc = Native.pollerAddSpotSub(handle, spotSub, item.userData,
            events);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_poller_add_spot_sub");
        registerItem(item);
    }

    public void addSpotSub(Spot spot, PollEventType... events) {
        addSpotSub(spot, PollEventType.combine(events), null);
    }

    public void addSpotSub(Spot spot, Object tag, PollEventType... events) {
        addSpotSub(spot, PollEventType.combine(events), tag);
    }

    public void addSpotPub(Spot spot, int events) {
        addSpotPub(spot, events, null);
    }

    public void addSpotPub(Spot spot, int events, Object tag) {
        ensureOpen();
        Objects.requireNonNull(spot, "spot");
        MemorySegment spotPub = spot.pubHandle();
        PollItem item = newPollItem(null, spotPub, 0, events, tag, true);
        int rc = Native.pollerAddSpotPub(handle, spotPub, item.userData,
            events);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_poller_add_spot_pub");
        registerItem(item);
    }

    public void addSpotPub(Spot spot, PollEventType... events) {
        addSpotPub(spot, PollEventType.combine(events), null);
    }

    public void addSpotPub(Spot spot, Object tag, PollEventType... events) {
        addSpotPub(spot, PollEventType.combine(events), tag);
    }

    public void addReceiver(Receiver receiver, int events) {
        addReceiver(receiver, events, null);
    }

    public void addReceiver(Receiver receiver, int events, Object tag) {
        ensureOpen();
        Objects.requireNonNull(receiver, "receiver");
        MemorySegment receiverHandle = receiver.handle();
        PollItem item = newPollItem(null, receiverHandle, 0, events, tag, true);
        int rc = Native.pollerAddReceiver(handle, receiverHandle,
            item.userData, events);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_poller_add_receiver");
        registerItem(item);
    }

    public void addReceiver(Receiver receiver, PollEventType... events) {
        addReceiver(receiver, PollEventType.combine(events), null);
    }

    public void addReceiver(Receiver receiver, Object tag,
                            PollEventType... events) {
        addReceiver(receiver, PollEventType.combine(events), tag);
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

    public void modifySpotSub(Spot spot, int events) {
        ensureOpen();
        Objects.requireNonNull(spot, "spot");
        MemorySegment spotSub = spot.subHandle();
        int index = findSocket(spotSub);
        if (index < 0)
            throw new IllegalArgumentException("spot sub is not registered");
        int rc = Native.pollerModifySpotSub(handle, spotSub, events);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_poller_modify_spot_sub");
        items.get(index).events = events;
    }

    public void modifySpotSub(Spot spot, PollEventType... events) {
        modifySpotSub(spot, PollEventType.combine(events));
    }

    public void modifySpotPub(Spot spot, int events) {
        ensureOpen();
        Objects.requireNonNull(spot, "spot");
        MemorySegment spotPub = spot.pubHandle();
        int index = findSocket(spotPub);
        if (index < 0)
            throw new IllegalArgumentException("spot pub is not registered");
        int rc = Native.pollerModifySpotPub(handle, spotPub, events);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_poller_modify_spot_pub");
        items.get(index).events = events;
    }

    public void modifySpotPub(Spot spot, PollEventType... events) {
        modifySpotPub(spot, PollEventType.combine(events));
    }

    public void modifyReceiver(Receiver receiver, int events) {
        ensureOpen();
        Objects.requireNonNull(receiver, "receiver");
        MemorySegment receiverHandle = receiver.handle();
        int index = findSocket(receiverHandle);
        if (index < 0)
            throw new IllegalArgumentException("receiver is not registered");
        int rc = Native.pollerModifyReceiver(handle, receiverHandle, events);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_poller_modify_receiver");
        items.get(index).events = events;
    }

    public void modifyReceiver(Receiver receiver, PollEventType... events) {
        modifyReceiver(receiver, PollEventType.combine(events));
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

    public boolean removeSpotSub(Spot spot) {
        ensureOpen();
        Objects.requireNonNull(spot, "spot");
        MemorySegment spotSub = spot.subHandle();
        int index = findSocket(spotSub);
        if (index < 0)
            return false;
        int rc = Native.pollerRemoveSpotSub(handle, spotSub);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_poller_remove_spot_sub");
        unregisterItem(index);
        return true;
    }

    public boolean removeSpotPub(Spot spot) {
        ensureOpen();
        Objects.requireNonNull(spot, "spot");
        MemorySegment spotPub = spot.pubHandle();
        int index = findSocket(spotPub);
        if (index < 0)
            return false;
        int rc = Native.pollerRemoveSpotPub(handle, spotPub);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_poller_remove_spot_pub");
        unregisterItem(index);
        return true;
    }

    public boolean removeReceiver(Receiver receiver) {
        ensureOpen();
        Objects.requireNonNull(receiver, "receiver");
        MemorySegment receiverHandle = receiver.handle();
        int index = findSocket(receiverHandle);
        if (index < 0)
            return false;
        int rc = Native.pollerRemoveReceiver(handle, receiverHandle);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_poller_remove_receiver");
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
        MemorySegment arr = ensureNativeEvents(items.size());
        int rc = Native.pollerWaitAll(handle, arr, items.size(), timeoutMs);
        if (rc < 0)
            throw ZlinkException.fromLastError("zlink_poller_wait_all");
        lastReadyCount = rc;
        ensureReadyCacheCapacity(rc);
        for (int i = 0; i < rc; i++) {
            long base = (long) i * POLLER_EVENT_SIZE;
            readyReventsCache[i] = nativeEvents.get(ValueLayout.JAVA_SHORT,
                base + EVENT_EVENTS_OFFSET);
            readyFdCache[i] = nativeEvents.get(ValueLayout.JAVA_INT,
                base + EVENT_FD_OFFSET);
            readyItemsCache[i] = resolveReadyItem(base);
        }
        return rc;
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
            nativeEvents = eventArena.allocate(POLLER_EVENT_SIZE
                * requiredCount, ValueLayout.ADDRESS.byteAlignment());
            nativeEventsCapacity = requiredCount;
        }
        return nativeEvents;
    }

    private long eventBase(int index) {
        ensureOpen();
        checkReadyIndex(index);
        if (nativeEvents == null || nativeEvents.address() == 0)
            throw new IllegalStateException("no ready events");
        return (long) index * POLLER_EVENT_SIZE;
    }

    private PollItem cachedReadyItem(int index) {
        checkReadyIndex(index);
        return readyItemsCache[index];
    }

    private PollItem resolveReadyItem(long base) {
        MemorySegment userData = nativeEvents.get(ValueLayout.ADDRESS,
            base + EVENT_USER_DATA_OFFSET);
        if (userData.address() != 0) {
            PollItem item = itemForUserData(userData);
            if (item != null) {
                return item;
            }
        }
        MemorySegment socketHandle = nativeEvents.get(ValueLayout.ADDRESS,
            base + EVENT_SOCKET_OFFSET);
        int fd = nativeEvents.get(ValueLayout.JAVA_INT,
            base + EVENT_FD_OFFSET);
        return socketHandle.address() != 0
            ? findSocketItem(socketHandle) : findFdItem(fd);
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

    private PollItem newPollItem(Socket socket, MemorySegment socketHandle,
                                 int fd, int events, Object tag,
                                 boolean isSocket) {
        long token = nextToken++;
        MemorySegment userData = tagArena.allocate(ValueLayout.JAVA_LONG);
        userData.set(ValueLayout.JAVA_LONG, 0, token);
        return new PollItem(socket, socketHandle, fd, events, tag, isSocket,
            token, userData);
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
        if (arena != null && arena.scope().isAlive()) {
            arena.close();
        }
    }

    public static final class PollItem {
        public final Socket socket;
        public final MemorySegment socketHandle;
        public final int fd;
        public int events;
        public final Object tag;
        public final boolean isSocket;
        public final long token;
        public final MemorySegment userData;

        PollItem(Socket socket, MemorySegment socketHandle, int fd, int events,
                 Object tag, boolean isSocket, long token,
                 MemorySegment userData) {
            this.socket = socket;
            this.socketHandle = socketHandle;
            this.fd = fd;
            this.events = events;
            this.tag = tag;
            this.isSocket = isSocket;
            this.token = token;
            this.userData = userData;
        }
    }

    public record PollEvent(Socket socket, int revents, int fd, Object tag,
                            int events) {
        public PollEvent(Socket socket, int revents) {
            this(socket, revents, 0, null, 0);
        }
    }
}
