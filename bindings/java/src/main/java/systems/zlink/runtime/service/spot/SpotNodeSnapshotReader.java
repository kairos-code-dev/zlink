/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import java.lang.foreign.Arena;
import java.lang.foreign.MemoryLayout;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.util.ArrayList;
import java.util.List;
import systems.zlink.runtime.nativeapi.InternalAccess;

final class SpotNodeSnapshotReader {
    private SpotNodeSnapshotReader() {
    }

    static <T> List<T> read(String operation, MemoryLayout layout,
                            NativeSnapshotReader reader,
                            NativeSnapshotDecoder<T> decoder) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment count = arena.allocate(ValueLayout.JAVA_LONG);
            int rc = reader.read(MemorySegment.NULL, count);
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(operation);
            }
            int available = SpotNodeSnapshots.boundedCount(
                count.get(ValueLayout.JAVA_LONG, 0));
            if (available == 0) {
                return List.of();
            }
            MemorySegment entries = arena.allocate(layout, available);
            count.set(ValueLayout.JAVA_LONG, 0, available);
            rc = reader.read(entries, count);
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(operation);
            }
            int actual = Math.min(available, SpotNodeSnapshots.boundedCount(
                count.get(ValueLayout.JAVA_LONG, 0)));
            long stride = layout.byteSize();
            ArrayList<T> out = new ArrayList<>(actual);
            for (int i = 0; i < actual; i++) {
                out.add(decoder.decode(entries.asSlice((long) i * stride,
                    stride)));
            }
            return List.copyOf(out);
        }
    }

    @FunctionalInterface
    interface NativeSnapshotReader {
        int read(MemorySegment entries, MemorySegment count);
    }

    @FunctionalInterface
    interface NativeSnapshotDecoder<T> {
        T decode(MemorySegment entry);
    }
}
