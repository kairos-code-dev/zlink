/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.service.gateway.Gateway;
import dev.kairoscode.zlink.service.receiver.Receiver;
import dev.kairoscode.zlink.service.spot.Spot;
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
    private int lastReadyCount = 0;
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

    public void addSpotSub(Spot spot, int events) {
        addSpotSub(spot, events, null);
    }

    public void addSpotSub(Spot spot, int events, Object tag) {
        ensureOpen();
        Objects.requireNonNull(spot, "spot");
        MemorySegment spotSub = spot.subHandle();
        int rc = Native.pollerAddSpotSub(handle, spotSub, MemorySegment.NULL,
            events);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_poller_add_spot_sub");
        items.add(new PollItem(null, spotSub, 0, events, tag, true));
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
        int rc = Native.pollerAddSpotPub(handle, spotPub, MemorySegment.NULL,
            events);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_poller_add_spot_pub");
        items.add(new PollItem(null, spotPub, 0, events, tag, true));
    }

    public void addSpotPub(Spot spot, PollEventType... events) {
        addSpotPub(spot, PollEventType.combine(events), null);
    }

    public void addSpotPub(Spot spot, Object tag, PollEventType... events) {
        addSpotPub(spot, PollEventType.combine(events), tag);
    }

    public void addGateway(Gateway gateway, int events) {
        addGateway(gateway, events, null);
    }

    public void addGateway(Gateway gateway, int events, Object tag) {
        ensureOpen();
        Objects.requireNonNull(gateway, "gateway");
        MemorySegment gatewayHandle = gateway.handle();
        int rc = Native.pollerAddGateway(handle, gatewayHandle,
            MemorySegment.NULL, events);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_poller_add_gateway");
        items.add(new PollItem(null, gatewayHandle, 0, events, tag, true));
    }

    public void addGateway(Gateway gateway, PollEventType... events) {
        addGateway(gateway, PollEventType.combine(events), null);
    }

    public void addGateway(Gateway gateway, Object tag,
                           PollEventType... events) {
        addGateway(gateway, PollEventType.combine(events), tag);
    }

    public void addReceiver(Receiver receiver, int events) {
        addReceiver(receiver, events, null);
    }

    public void addReceiver(Receiver receiver, int events, Object tag) {
        ensureOpen();
        Objects.requireNonNull(receiver, "receiver");
        MemorySegment receiverHandle = receiver.handle();
        int rc = Native.pollerAddReceiver(handle, receiverHandle,
            MemorySegment.NULL, events);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_poller_add_receiver");
        items.add(new PollItem(null, receiverHandle, 0, events, tag, true));
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

    public void modifyGateway(Gateway gateway, int events) {
        ensureOpen();
        Objects.requireNonNull(gateway, "gateway");
        MemorySegment gatewayHandle = gateway.handle();
        int index = findSocket(gatewayHandle);
        if (index < 0)
            throw new IllegalArgumentException("gateway is not registered");
        int rc = Native.pollerModifyGateway(handle, gatewayHandle, events);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_poller_modify_gateway");
        items.get(index).events = events;
    }

    public void modifyGateway(Gateway gateway, PollEventType... events) {
        modifyGateway(gateway, PollEventType.combine(events));
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
        items.remove(index);
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
        items.remove(index);
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
        items.remove(index);
        return true;
    }

    public boolean removeGateway(Gateway gateway) {
        ensureOpen();
        Objects.requireNonNull(gateway, "gateway");
        MemorySegment gatewayHandle = gateway.handle();
        int index = findSocket(gatewayHandle);
        if (index < 0)
            return false;
        int rc = Native.pollerRemoveGateway(handle, gatewayHandle);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_poller_remove_gateway");
        items.remove(index);
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
        lastReadyCount = 0;
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
            PollItem item = readyItem(i);
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
        PollItem item = readyItem(index);
        return item == null ? null : item.socket;
    }

    public Object readyTag(int index) {
        PollItem item = readyItem(index);
        return item == null ? null : item.tag;
    }

    public int readyFd(int index) {
        long base = eventBase(index);
        return nativeEvents.get(ValueLayout.JAVA_INT, base + EVENT_FD_OFFSET);
    }

    public int readyEvents(int index) {
        PollItem item = readyItem(index);
        return item == null ? readyRevents(index) : item.events;
    }

    public short readyRevents(int index) {
        long base = eventBase(index);
        return nativeEvents.get(ValueLayout.JAVA_SHORT,
            base + EVENT_EVENTS_OFFSET);
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
        lastReadyCount = 0;
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
        if (index < 0 || index >= lastReadyCount)
            throw new IndexOutOfBoundsException("ready index " + index);
        if (nativeEvents == null || nativeEvents.address() == 0)
            throw new IllegalStateException("no ready events");
        return (long) index * POLLER_EVENT_SIZE;
    }

    private PollItem readyItem(int index) {
        long base = eventBase(index);
        MemorySegment socketHandle = nativeEvents.get(ValueLayout.ADDRESS,
            base + EVENT_SOCKET_OFFSET);
        int fd = nativeEvents.get(ValueLayout.JAVA_INT,
            base + EVENT_FD_OFFSET);
        return socketHandle.address() != 0
            ? findSocketItem(socketHandle) : findFdItem(fd);
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
