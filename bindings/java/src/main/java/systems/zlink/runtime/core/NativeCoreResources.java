/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.core;

import systems.zlink.contracts.core.AtomicCounter;
import systems.zlink.contracts.core.Stopwatch;
import systems.zlink.contracts.core.ZlinkThread;

final class NativeCoreResources {
    private NativeCoreResources() {
    }

    static AtomicCounter createAtomicCounter() {
        return new NativeAtomicCounter();
    }

    static Stopwatch createStopwatch() {
        return new NativeStopwatch();
    }

    static ZlinkThread createThread(Runnable task) {
        return new NativeZlinkThread(task);
    }
}
