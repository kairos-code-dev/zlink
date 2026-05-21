/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import java.time.Duration;
import java.util.List;
import java.util.Objects;
import systems.zlink.contracts.PollEvents;
import systems.zlink.contracts.PollEventFlag;
import systems.zlink.contracts.Poller;
import systems.zlink.contracts.Socket;
import systems.zlink.contracts.ZlinkException;

public final class PerfSocketPollSet implements AutoCloseable {
    private static final int ERRNO_EINTR = 4;
    private static final int ERRNO_EAGAIN = 11;
    private static final int ERRNO_EWOULDBLOCK_WIN = 10035;
    private final Socket[] sockets;
    private final boolean[] readyIn;
    private final boolean[] readyOut;
    private final boolean[] readyErr;
    private final boolean[] readyPri;
    private final int[] readyIndexes;
    private final int[] currentMasks;
    private final Poller poller = new Poller();
    private final PollEvents readyEventsBuffer;
    private int readyCount;

    private PerfSocketPollSet(List<Socket> sockets,
                              PollEventFlag... initialEvents) {
        this.sockets = sockets.toArray(Socket[]::new);
        this.readyIn = new boolean[this.sockets.length];
        this.readyOut = new boolean[this.sockets.length];
        this.readyErr = new boolean[this.sockets.length];
        this.readyPri = new boolean[this.sockets.length];
        this.readyIndexes = new int[this.sockets.length];
        this.currentMasks = new int[this.sockets.length];
        this.readyEventsBuffer = new PollEvents(
            Math.max(1, this.sockets.length));
        clearReadyEvents();
        int initialMask = mask(initialEvents);
        for (int i = 0; i < this.sockets.length; i++) {
            Socket socket = Objects.requireNonNull(this.sockets[i], "socket");
            poller.add(socket, i, initialEvents);
            currentMasks[i] = initialMask;
        }
    }

    public static PerfSocketPollSet fromSockets(List<Socket> sockets,
                                                PollEventFlag... initialEvents) {
        Objects.requireNonNull(sockets, "sockets");
        return new PerfSocketPollSet(sockets, initialEvents);
    }

    public void setEvents(int index, PollEventFlag... newEvents) {
        checkIndex(index);
        int newMask = mask(newEvents);
        if (currentMasks[index] == newMask) {
            return;
        }
        poller.modify(sockets[index], newEvents);
        currentMasks[index] = newMask;
    }

    public boolean isReady(int index, PollEventFlag event) {
        checkIndex(index);
        return switch (event) {
            case POLLIN -> readyIn[index];
            case POLLOUT -> readyOut[index];
            case POLLERR -> readyErr[index];
            case POLLPRI -> readyPri[index];
            case POLLCOMPLETION -> false;
        };
    }

    public int readyCount() {
        return readyCount;
    }

    public int readyIndexAt(int offset) {
        if (offset < 0 || offset >= readyCount) {
            throw new IndexOutOfBoundsException("ready offset " + offset);
        }
        return readyIndexes[offset];
    }

    public int poll(int timeoutMs) {
        clearReadyEvents();
        try {
            poller.wait(readyEventsBuffer, Duration.ofMillis(timeoutMs));
        } catch (ZlinkException ex) {
            int errno = ex.getInternalErrno();
            if (errno == ERRNO_EINTR
                || errno == ERRNO_EAGAIN
                || errno == ERRNO_EWOULDBLOCK_WIN) {
                return 0;
            }
            throw ex;
        }
        int count = readyEventsBuffer.readyCount();
        for (int i = 0; i < count; i++) {
            long slot = readyEventsBuffer.slot(i);
            if (slot >= 0 && slot < sockets.length) {
                int index = (int) slot;
                boolean hasIn = readyEventsBuffer.hasEvent(i,
                    PollEventFlag.POLLIN);
                boolean hasOut = readyEventsBuffer.hasEvent(i,
                    PollEventFlag.POLLOUT);
                boolean hasErr = readyEventsBuffer.hasEvent(i,
                    PollEventFlag.POLLERR);
                boolean hasPri = readyEventsBuffer.hasEvent(i,
                    PollEventFlag.POLLPRI);
                readyIn[index] = hasIn;
                readyOut[index] = hasOut;
                readyErr[index] = hasErr;
                readyPri[index] = hasPri;
                if (hasIn || hasOut || hasErr || hasPri) {
                    readyIndexes[readyCount++] = index;
                }
            }
        }
        return readyCount;
    }

    @Override
    public void close() {
        poller.close();
    }

    // PERF_MULTI § 1.1 "Ready source dispatch": clear only the entries that
    // were actually dispatched ready on the previous wake, not every socket.
    // The previous-wake ready set is exactly readyIndexes[0..readyCount); C/C++
    // reset just the dispatched ready entries. This avoids the O(N) per-wake
    // Arrays.fill over all four boolean arrays regardless of socket count.
    private void clearReadyEvents() {
        for (int i = 0; i < readyCount; i++) {
            int index = readyIndexes[i];
            readyIn[index] = false;
            readyOut[index] = false;
            readyErr[index] = false;
            readyPri[index] = false;
        }
        readyCount = 0;
    }

    private void checkIndex(int index) {
        if (index < 0 || index >= sockets.length) {
            throw new IndexOutOfBoundsException("index " + index);
        }
    }

    private static int mask(PollEventFlag... events) {
        int mask = 0;
        if (events == null) {
            return mask;
        }
        for (PollEventFlag event : events) {
            mask |= switch (event) {
                case POLLIN -> 1;
                case POLLOUT -> 2;
                case POLLERR -> 4;
                case POLLPRI -> 8;
                case POLLCOMPLETION -> 32;
            };
        }
        return mask;
    }

}
