/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import java.lang.foreign.MemoryLayout;
import java.lang.foreign.MemorySegment;
import java.util.List;
import systems.zlink.runtime.nativeapi.NativeListSnapshots;

final class SpotNodeSnapshotReader {
    private SpotNodeSnapshotReader() {
    }

    static <T> List<T> read(String operation, MemoryLayout layout,
                            NativeSnapshotReader reader,
                            NativeSnapshotDecoder<T> decoder) {
        return NativeListSnapshots.read(layout, operation,
            (arena, entries, count) -> reader.read(entries, count),
            decoder::decode);
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
