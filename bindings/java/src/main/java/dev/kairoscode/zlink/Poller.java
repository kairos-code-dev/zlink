/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.util.ArrayList;
import java.util.List;

public final class Poller {
    private static final long POLL_ITEM_SIZE = 24;
    private static final long POLL_SOCKET_OFFSET = 0;
    private static final long POLL_FD_OFFSET = 8;
    private static final long POLL_EVENTS_OFFSET = 12;
    private static final long POLL_REVENTS_OFFSET = 14;

    private final List<PollItem> items = new ArrayList<>();
    private final Arena pollArena = Arena.ofAuto();
    private MemorySegment nativeItems = MemorySegment.NULL;
    private int nativeItemsCapacity = 0;

    public void add(Socket socket, int events) {
        items.add(new PollItem(socket, 0, events));
    }

    public void add(Socket socket, PollEventType... events) {
        items.add(new PollItem(socket, 0, PollEventType.combine(events)));
    }

    public void addFd(int fd, int events) {
        items.add(new PollItem(null, fd, events));
    }

    public void addFd(int fd, PollEventType... events) {
        items.add(new PollItem(null, fd, PollEventType.combine(events)));
    }

    public int pollCount(int timeoutMs) {
        if (items.isEmpty())
            return 0;
        MemorySegment arr = prepareNativeItems();
        return Native.pollRaw(arr, items.size(), timeoutMs);
    }

    public boolean pollAny(int timeoutMs) {
        return pollCount(timeoutMs) > 0;
    }

    public List<PollEvent> poll(int timeoutMs) {
        if (items.isEmpty())
            return List.of();

        MemorySegment arr = prepareNativeItems();
        int readyCount = Native.pollRaw(arr, items.size(), timeoutMs);
        if (readyCount == 0)
            return List.of();

        List<PollEvent> out = new ArrayList<>(Math.min(readyCount, items.size()));
        for (int i = 0; i < items.size(); i++) {
            long base = (long) i * POLL_ITEM_SIZE;
            short revents = arr.get(ValueLayout.JAVA_SHORT, base + POLL_REVENTS_OFFSET);
            if (revents != 0)
                out.add(new PollEvent(items.get(i).socket, revents));
        }
        return out;
    }

    private MemorySegment prepareNativeItems() {
        int count = items.size();
        MemorySegment arr = ensureNativeItems(count);
        for (int i = 0; i < count; i++) {
            PollItem item = items.get(i);
            long base = (long) i * POLL_ITEM_SIZE;
            MemorySegment socket = item.socket == null
                ? MemorySegment.NULL : item.socket.handle();
            arr.set(ValueLayout.ADDRESS, base + POLL_SOCKET_OFFSET, socket);
            arr.set(ValueLayout.JAVA_INT, base + POLL_FD_OFFSET, item.fd);
            arr.set(ValueLayout.JAVA_SHORT, base + POLL_EVENTS_OFFSET,
                (short) item.events);
            arr.set(ValueLayout.JAVA_SHORT, base + POLL_REVENTS_OFFSET,
                (short) 0);
        }
        return arr;
    }

    private MemorySegment ensureNativeItems(int requiredCount) {
        if (nativeItemsCapacity < requiredCount) {
            nativeItems = pollArena.allocate(POLL_ITEM_SIZE * requiredCount);
            nativeItemsCapacity = requiredCount;
        }
        return nativeItems;
    }

    public static final class PollItem {
        public final Socket socket;
        public final int fd;
        public final int events;

        PollItem(Socket socket, int fd, int events) {
            this.socket = socket;
            this.fd = fd;
            this.events = events;
        }
    }

    public record PollEvent(Socket socket, int revents) {}
}
