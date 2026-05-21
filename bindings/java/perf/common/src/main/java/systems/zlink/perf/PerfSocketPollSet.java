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
    private static final int MASK_POLLIN = 1;
    private static final int MASK_POLLOUT = 2;
    private static final int MASK_POLLERR = 4;
    private static final int MASK_POLLPRI = 8;
    private static final int MASK_POLLCOMPLETION = 32;
    private static final Duration WAIT_FOREVER = Duration.ofMillis(-1);
    private final Socket[] sockets;
    private final int[] readyIndexes;
    private final int[] readyMasks;
    private final int[] currentMasks;
    private final Poller poller = new Poller();
    private final PollEvents readyEventsBuffer;
    private int readyCount;

    private PerfSocketPollSet(List<Socket> sockets,
                              PollEventFlag... initialEvents) {
        this.sockets = sockets.toArray(Socket[]::new);
        this.readyIndexes = new int[this.sockets.length];
        this.readyMasks = new int[this.sockets.length];
        this.currentMasks = new int[this.sockets.length];
        this.readyEventsBuffer = new PollEvents(
            Math.max(1, this.sockets.length));
        readyCount = 0;
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

    public int readyCount() {
        return readyCount;
    }

    public int readyIndexAt(int offset) {
        if (offset < 0 || offset >= readyCount) {
            throw new IndexOutOfBoundsException("ready offset " + offset);
        }
        return readyIndexes[offset];
    }

    public int readyMaskAt(int offset) {
        if (offset < 0 || offset >= readyCount) {
            throw new IndexOutOfBoundsException("ready offset " + offset);
        }
        return readyMasks[offset];
    }

    public boolean readyHasEventAt(int offset, PollEventFlag event) {
        return (readyMaskAt(offset) & maskOne(event)) != 0;
    }

    public int poll(int timeoutMs) {
        readyCount = 0;
        try {
            poller.wait(readyEventsBuffer,
                timeoutMs == -1 ? WAIT_FOREVER : Duration.ofMillis(timeoutMs));
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
                int readyMask = readyEventsBuffer.revents(i);
                if (readyMask != 0) {
                    readyIndexes[readyCount++] = index;
                    readyMasks[readyCount - 1] = readyMask;
                }
            }
        }
        return readyCount;
    }

    @Override
    public void close() {
        poller.close();
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
                case POLLOUT -> MASK_POLLOUT;
                case POLLERR -> MASK_POLLERR;
                case POLLPRI -> MASK_POLLPRI;
                case POLLCOMPLETION -> MASK_POLLCOMPLETION;
            };
        }
        return mask;
    }

    private static int maskOne(PollEventFlag event) {
        return switch (event) {
            case POLLIN -> MASK_POLLIN;
            case POLLOUT -> MASK_POLLOUT;
            case POLLERR -> MASK_POLLERR;
            case POLLPRI -> MASK_POLLPRI;
            case POLLCOMPLETION -> MASK_POLLCOMPLETION;
        };
    }

}
