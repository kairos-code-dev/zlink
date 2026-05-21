/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.util.Objects;

public final class PollEvents {
    static final long POLLER_EVENT_SIZE = 48;
    private static final long EVENT_SOURCE_KIND_OFFSET = 0;
    private static final long EVENT_FD_OFFSET = 16;
    private static final long EVENT_USER_DATA_OFFSET = 32;
    private static final long EVENT_EVENTS_OFFSET = 40;

    private final MemorySegment segment;
    private final int capacity;
    private int readyCount;

    public PollEvents(int capacity) {
        if (capacity <= 0)
            throw new IllegalArgumentException("capacity must be > 0");
        this.capacity = capacity;
        this.segment = Arena.ofAuto().allocate(POLLER_EVENT_SIZE * capacity,
            ValueLayout.ADDRESS.byteAlignment());
    }

    public int capacity() {
        return capacity;
    }

    public int readyCount() {
        return readyCount;
    }

    public PollSourceKind sourceKind(int index) {
        checkReadyIndex(index);
        return PollSourceKind.fromValue(segment.get(ValueLayout.JAVA_INT,
            offset(index, EVENT_SOURCE_KIND_OFFSET)));
    }

    public long slot(int index) {
        checkReadyIndex(index);
        return segment.get(ValueLayout.ADDRESS,
            offset(index, EVENT_USER_DATA_OFFSET)).address();
    }

    public int revents(int index) {
        checkReadyIndex(index);
        return segment.get(ValueLayout.JAVA_SHORT,
            offset(index, EVENT_EVENTS_OFFSET));
    }

    public boolean hasEvent(int index, PollEventFlag event) {
        Objects.requireNonNull(event, "event");
        return (revents(index) & event.mask()) != 0;
    }

    public int fd(int index) {
        checkReadyIndex(index);
        return segment.get(ValueLayout.JAVA_INT, offset(index, EVENT_FD_OFFSET));
    }

    MemorySegment segment() {
        return segment;
    }

    void markReadyCount(int readyCount) {
        if (readyCount < 0 || readyCount > capacity)
            throw new IllegalArgumentException("readyCount out of range");
        this.readyCount = readyCount;
    }

    private void checkReadyIndex(int index) {
        if (index < 0 || index >= readyCount)
            throw new IndexOutOfBoundsException("ready index " + index);
    }

    private static long offset(int index, long fieldOffset) {
        return (long) index * POLLER_EVENT_SIZE + fieldOffset;
    }
}
