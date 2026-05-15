/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

final class RecvScratch {
    final Arena arena = Arena.ofAuto();
    final MemorySegment sourceRidOut = arena.allocate(ValueLayout.ADDRESS);
    final MemorySegment hasMoreOut = arena.allocate(ValueLayout.JAVA_INT);
}
