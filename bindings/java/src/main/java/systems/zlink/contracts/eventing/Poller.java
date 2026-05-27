/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.eventing;

import systems.zlink.contracts.sockets.Socket;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.errors.ZlinkException;
import java.lang.foreign.MemorySegment;
import java.time.Duration;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.Native;
import systems.zlink.runtime.nativeapi.RequestProgressPump;

public final class Poller implements AutoCloseable {
    private static final int MASK_POLLIN = PollEventFlag.POLLIN.mask();
    private static final int MASK_POLLOUT = PollEventFlag.POLLOUT.mask();
    private static final int MASK_POLLCOMPLETION = 32;
    private final List<PollItem> items = new ArrayList<>();
    private final Map<Long, Integer> socketIndexes = new HashMap<>();
    private final Map<Long, Integer> spotIndexes = new HashMap<>();
    private MemorySegment handle;

    public Poller() {
        handle = Native.pollerNew();
        if (handle == null || handle.address() == 0)
            throw ZlinkException.fromLastError("zlink_poller_new");
    }

    public void add(Socket socket, long slot, PollEventFlag... events) {
        addSocket(socket, PollEventFlag.combine(events), slot);
    }

    public void add(Spot spot, long slot, PollEventFlag... events) {
        addSpot(spot, PollEventFlag.combine(events), slot);
    }

    public void addFd(int fd, long slot, PollEventFlag... events) {
        ensureOpen();
        validateSlot(slot);
        PollItem item = PollItem.fd(fd, PollEventFlag.combine(events), slot);
        int rc = Native.pollerAddFd(handle, fd, item.userData(), item.events);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_poller_add_fd");
        items.add(item);
    }

    public void add(Timer timer, long slot) {
        ensureOpen();
        Objects.requireNonNull(timer, "timer");
        validateSlot(slot);
        PollItem item = PollItem.timer(timer.handle(), slot);
        int rc = Native.pollerAddTimer(handle, timer.handle(), item.userData());
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_poller_add_timer");
        items.add(item);
    }

    public void modify(Socket socket, PollEventFlag... events) {
        ensureOpen();
        Objects.requireNonNull(socket, "socket");
        int index = findSocket(InternalAccess.socketHandle(socket));
        if (index < 0)
            throw new IllegalArgumentException("socket is not registered");
        int mask = PollEventFlag.combine(events);
        int rc = Native.pollerModify(handle, InternalAccess.socketHandle(socket), mask);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_poller_modify");
        items.get(index).events = mask;
    }

    public void modify(Spot spot, PollEventFlag... events) {
        ensureOpen();
        Objects.requireNonNull(spot, "spot");
        int index = findSpot(InternalAccess.spotHandle(spot));
        if (index < 0)
            throw new IllegalArgumentException("spot is not registered");
        int mask = PollEventFlag.combine(events);
        PollItem item = items.get(index);
        boolean nextSpotPub = spotPollUsesPubPlane(mask);
        int rc;
        if (item.spotPub == nextSpotPub) {
            rc = item.spotPub
                ? Native.pollerModifySpotPub(handle, InternalAccess.spotHandle(spot),
                    mask)
                : Native.pollerModifySpotSub(handle, InternalAccess.spotHandle(spot),
                    mask);
        } else {
            rc = item.spotPub
                ? Native.pollerRemoveSpotPub(handle, InternalAccess.spotHandle(spot))
                : Native.pollerRemoveSpotSub(handle, InternalAccess.spotHandle(spot));
            if (rc == 0) {
                rc = nextSpotPub
                    ? Native.pollerAddSpotPub(handle, InternalAccess.spotHandle(spot),
                        item.userData(), mask)
                    : Native.pollerAddSpotSub(handle, InternalAccess.spotHandle(spot),
                        item.userData(), mask);
            }
            item.spotPub = nextSpotPub;
        }
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_poller_modify");
        unregisterExternalProgress(item);
        item.events = mask;
        registerExternalProgress(item);
    }

    public void modifyFd(int fd, PollEventFlag... events) {
        ensureOpen();
        int index = findFd(fd);
        if (index < 0)
            throw new IllegalArgumentException("fd is not registered");
        int mask = PollEventFlag.combine(events);
        int rc = Native.pollerModifyFd(handle, fd, mask);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_poller_modify_fd");
        items.get(index).events = mask;
    }

    public boolean remove(Socket socket) {
        ensureOpen();
        Objects.requireNonNull(socket, "socket");
        int index = findSocket(InternalAccess.socketHandle(socket));
        if (index < 0)
            return false;
        int rc = Native.pollerRemove(handle, InternalAccess.socketHandle(socket));
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_poller_remove");
        PollItem removed = items.remove(index);
        unregisterExternalProgress(removed);
        socketIndexes.remove(removed.handle.address());
        refreshIndexesFrom(index);
        return true;
    }

    public boolean remove(Spot spot) {
        ensureOpen();
        Objects.requireNonNull(spot, "spot");
        int index = findSpot(InternalAccess.spotHandle(spot));
        if (index < 0)
            return false;
        PollItem item = items.get(index);
        int rc = item.spotPub
            ? Native.pollerRemoveSpotPub(handle, InternalAccess.spotHandle(spot))
            : Native.pollerRemoveSpotSub(handle, InternalAccess.spotHandle(spot));
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_poller_remove");
        PollItem removed = items.remove(index);
        unregisterExternalProgress(removed);
        spotIndexes.remove(removed.handle.address());
        refreshIndexesFrom(index);
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
        unregisterExternalProgress(items.remove(index));
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
        unregisterExternalProgress(items.remove(index));
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
        unregisterAllExternalProgress();
        items.clear();
        socketIndexes.clear();
        spotIndexes.clear();
    }

    public int size() {
        ensureOpen();
        int rc = Native.pollerSize(handle);
        if (rc < 0)
            throw ZlinkException.fromLastError("zlink_poller_size");
        return rc;
    }

    public int wait(PollEvents events, Duration timeout) {
        ensureOpen();
        Objects.requireNonNull(events, "events");
        if (items.isEmpty()) {
            events.markReadyCount(0);
            return 0;
        }
        int readyCount = Native.pollerWait(handle, events.segment(),
            events.capacity(), toIntMillis(timeout));
        if (readyCount < 0)
            throw ZlinkException.fromLastError("zlink_poller_wait");
        events.markReadyCount(readyCount);
        return readyCount;
    }

    @Override
    public void close() {
        if (handle == null || handle.address() == 0)
            return;
        Native.pollerDestroy(handle);
        handle = MemorySegment.NULL;
        unregisterAllExternalProgress();
        items.clear();
    }

    private void addSocket(Socket socket, int events, long slot) {
        ensureOpen();
        Objects.requireNonNull(socket, "socket");
        validateSlot(slot);
        PollItem item = PollItem.socket(InternalAccess.socketHandle(socket), events, slot);
        int rc = Native.pollerAdd(handle, InternalAccess.socketHandle(socket), item.userData(),
            events);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_poller_add");
        socketIndexes.putIfAbsent(InternalAccess.socketHandle(socket).address(), items.size());
        items.add(item);
    }

    private void addSpot(Spot spot, int events, long slot) {
        ensureOpen();
        Objects.requireNonNull(spot, "spot");
        validateSlot(slot);
        boolean spotPub = spotPollUsesPubPlane(events);
        PollItem item = PollItem.spot(InternalAccess.spotHandle(spot), events, slot,
            spotPub);
        int rc = spotPub
            ? Native.pollerAddSpotPub(handle, InternalAccess.spotHandle(spot),
                item.userData(), events)
            : Native.pollerAddSpotSub(handle, InternalAccess.spotHandle(spot),
                item.userData(), events);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_poller_add");
        registerExternalProgress(item);
        spotIndexes.putIfAbsent(InternalAccess.spotHandle(spot).address(), items.size());
        items.add(item);
    }

    private static boolean spotPollUsesPubPlane(int events) {
        return (events & MASK_POLLOUT) != 0
            && (events & (MASK_POLLIN | MASK_POLLCOMPLETION)) == 0;
    }

    private static void registerExternalProgress(PollItem item) {
        if (item.isSpot && (item.events & MASK_POLLCOMPLETION) != 0) {
            RequestProgressPump.acquireExternalSpotProgress(item.handle);
        }
    }

    private static void unregisterExternalProgress(PollItem item) {
        if (item.isSpot && (item.events & MASK_POLLCOMPLETION) != 0) {
            RequestProgressPump.releaseExternalSpotProgress(item.handle);
        }
    }

    private void unregisterAllExternalProgress() {
        for (PollItem item : items) {
            unregisterExternalProgress(item);
        }
    }

    private void ensureOpen() {
        if (handle == null || handle.address() == 0)
            throw new IllegalStateException("poller is closed");
    }

    private static void validateSlot(long slot) {
        if (slot < 0)
            throw new IllegalArgumentException("slot must be >= 0");
    }

    private static int toIntMillis(Duration timeout) {
        long millis = Objects.requireNonNull(timeout, "timeout").toMillis();
        if (millis < Integer.MIN_VALUE || millis > Integer.MAX_VALUE) {
            throw new IllegalArgumentException(
                "timeout millis out of int range: " + millis);
        }
        return (int) millis;
    }

    private int findSocket(MemorySegment socketHandle) {
        return socketIndexes.getOrDefault(socketHandle.address(), -1);
    }

    private int findFd(int fd) {
        for (int i = 0; i < items.size(); i++) {
            PollItem item = items.get(i);
            if (item.kind == PollSourceKind.FD && item.fd == fd)
                return i;
        }
        return -1;
    }

    private int findSpot(MemorySegment spotHandle) {
        return spotIndexes.getOrDefault(spotHandle.address(), -1);
    }

    private void refreshIndexesFrom(int start) {
        for (int i = start; i < items.size(); i++) {
            PollItem item = items.get(i);
            if (item.kind != PollSourceKind.SOCKET)
                continue;
            if (item.isSpot)
                spotIndexes.put(item.handle.address(), i);
            else
                socketIndexes.put(item.handle.address(), i);
        }
    }

    private int findTimer(MemorySegment timerHandle) {
        for (int i = 0; i < items.size(); i++) {
            PollItem item = items.get(i);
            if (item.kind == PollSourceKind.TIMER
                && item.handle.address() == timerHandle.address()) {
                return i;
            }
        }
        return -1;
    }

    private static final class PollItem {
        private final PollSourceKind kind;
        private final MemorySegment handle;
        private final int fd;
        private int events;
        private final long slot;
        private final boolean isSpot;
        private boolean spotPub;

        private PollItem(PollSourceKind kind, MemorySegment handle, int fd,
                         int events, long slot, boolean isSpot,
                         boolean spotPub) {
            this.kind = kind;
            this.handle = handle;
            this.fd = fd;
            this.events = events;
            this.slot = slot;
            this.isSpot = isSpot;
            this.spotPub = spotPub;
        }

        static PollItem socket(MemorySegment handle, int events, long slot) {
            return new PollItem(PollSourceKind.SOCKET, handle, 0, events,
                slot, false, false);
        }

        static PollItem spot(MemorySegment handle, int events, long slot,
                             boolean spotPub) {
            return new PollItem(PollSourceKind.SOCKET, handle, 0, events,
                slot, true, spotPub);
        }

        static PollItem fd(int fd, int events, long slot) {
            return new PollItem(PollSourceKind.FD, MemorySegment.NULL, fd,
                events, slot, false, false);
        }

        static PollItem timer(MemorySegment handle, long slot) {
            return new PollItem(PollSourceKind.TIMER, handle, 0, 0, slot,
                false, false);
        }

        MemorySegment userData() {
            return MemorySegment.ofAddress(slot);
        }
    }
}
