/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf;

import systems.zlink.Poller;
import systems.zlink.PollEvent;
import systems.zlink.PollEventFlag;
import systems.zlink.Socket;
import systems.zlink.ZlinkException;
import java.time.Duration;
import java.util.Arrays;
import java.util.EnumSet;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;

public final class PerfSocketPollSet implements AutoCloseable {
    private static final int ERRNO_EINTR = 4;
    private static final int ERRNO_EAGAIN = 11;
    private static final int ERRNO_EWOULDBLOCK_WIN = 10035;
    private final Socket[] sockets;
    private final EnumSet<PollEventFlag>[] readyEvents;
    private final Poller poller = new Poller();
    private final Map<Socket, Integer> indices = new IdentityHashMap<>();

    @SuppressWarnings("unchecked")
    private PerfSocketPollSet(List<Socket> sockets,
                              PollEventFlag... initialEvents) {
        this.sockets = sockets.toArray(Socket[]::new);
        this.readyEvents = new EnumSet[this.sockets.length];
        clearReadyEvents();
        for (int i = 0; i < this.sockets.length; i++) {
            Socket socket = Objects.requireNonNull(this.sockets[i], "socket");
            poller.add(socket, initialEvents);
            indices.put(socket, i);
        }
    }

    public static PerfSocketPollSet fromSockets(List<Socket> sockets,
                                                PollEventFlag... initialEvents) {
        Objects.requireNonNull(sockets, "sockets");
        return new PerfSocketPollSet(sockets, initialEvents);
    }

    public void setEvents(int index, PollEventFlag... newEvents) {
        checkIndex(index);
        poller.modify(sockets[index], newEvents);
    }

    public boolean isReady(int index, PollEventFlag event) {
        checkIndex(index);
        return readyEvents[index].contains(event);
    }

    public int poll(int timeoutMs) {
        clearReadyEvents();
        List<PollEvent> events;
        try {
            events = poller.waitAll(sockets.length, Duration.ofMillis(timeoutMs));
        } catch (ZlinkException ex) {
            int errno = ex.getInternalErrno();
            if (errno == ERRNO_EINTR
                || errno == ERRNO_EAGAIN
                || errno == ERRNO_EWOULDBLOCK_WIN) {
                return 0;
            }
            throw ex;
        }
        for (PollEvent event : events) {
            Socket socket = event.socket();
            Integer index = socket == null ? null : indices.get(socket);
            if (index != null) {
                readyEvents[index] = EnumSet.copyOf(event.revents());
            }
        }
        return events.size();
    }

    @Override
    public void close() {
        poller.close();
    }

    private void clearReadyEvents() {
        Arrays.setAll(readyEvents,
            ignored -> EnumSet.noneOf(PollEventFlag.class));
    }

    private void checkIndex(int index) {
        if (index < 0 || index >= sockets.length) {
            throw new IndexOutOfBoundsException("index " + index);
        }
    }

}
