/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf;

import systems.zlink.Poller;
import systems.zlink.PollEvent;
import systems.zlink.PollEventFlag;
import systems.zlink.Socket;
import systems.zlink.ZlinkException;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Objects;

public final class PerfSocketPollSet implements AutoCloseable {
    private static final int ERRNO_EINTR = 4;
    private static final int ERRNO_EAGAIN = 11;
    private static final int ERRNO_EWOULDBLOCK_WIN = 10035;
    private final Socket[] sockets;
    private final boolean[] readyIn;
    private final boolean[] readyOut;
    private final boolean[] readyErr;
    private final boolean[] readyPri;
    private final int[] currentMasks;
    private final List<PollEvent> events;
    private final Poller poller = new Poller();

    private PerfSocketPollSet(List<Socket> sockets,
                              PollEventFlag... initialEvents) {
        this.sockets = sockets.toArray(Socket[]::new);
        this.readyIn = new boolean[this.sockets.length];
        this.readyOut = new boolean[this.sockets.length];
        this.readyErr = new boolean[this.sockets.length];
        this.readyPri = new boolean[this.sockets.length];
        this.currentMasks = new int[this.sockets.length];
        this.events = new ArrayList<>(this.sockets.length);
        clearReadyEvents();
        int initialMask = mask(initialEvents);
        for (int i = 0; i < this.sockets.length; i++) {
            Socket socket = Objects.requireNonNull(this.sockets[i], "socket");
            poller.add(socket, Integer.valueOf(i), initialEvents);
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
        };
    }

    public int poll(int timeoutMs) {
        clearReadyEvents();
        int eventCount;
        try {
            eventCount = poller.wait(events, Duration.ofMillis(timeoutMs));
        } catch (ZlinkException ex) {
            int errno = ex.getInternalErrno();
            if (errno == ERRNO_EINTR
                || errno == ERRNO_EAGAIN
                || errno == ERRNO_EWOULDBLOCK_WIN) {
                return 0;
            }
            throw ex;
        }
        int count = Math.min(eventCount, sockets.length);
        for (int i = 0; i < count; i++) {
            PollEvent event = events.get(i);
            Object tag = event.tag();
            Integer index = tag instanceof Integer readyIndex
                ? readyIndex
                : null;
            if (index != null) {
                readyIn[index] = event.revents().contains(PollEventFlag.POLLIN);
                readyOut[index] = event.revents().contains(PollEventFlag.POLLOUT);
                readyErr[index] = event.revents().contains(PollEventFlag.POLLERR);
                readyPri[index] = event.revents().contains(PollEventFlag.POLLPRI);
            }
        }
        return count;
    }

    @Override
    public void close() {
        poller.close();
    }

    private void clearReadyEvents() {
        Arrays.fill(readyIn, false);
        Arrays.fill(readyOut, false);
        Arrays.fill(readyErr, false);
        Arrays.fill(readyPri, false);
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
            };
        }
        return mask;
    }

}
