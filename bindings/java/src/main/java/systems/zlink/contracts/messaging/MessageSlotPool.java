/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.messaging;

import systems.zlink.internal.ContractAccess;
import sun.misc.Unsafe;

final class MessageSlotPool {
    private static final Unsafe UNSAFE = UnsafeAccess.get();
    private static final long MESSAGE_LAYOUT_SIZE =
        ContractAccess.nativeMessageLayoutSize();
    private static final ThreadLocal<Pool> POOL =
        ThreadLocal.withInitial(Pool::new);

    private MessageSlotPool() {
    }

    static long acquire() {
        return POOL.get().acquire();
    }

    static void release(long slot) {
        POOL.get().release(slot);
    }

    private static final class Pool {
        private static final int CAPACITY = 32;
        private final long[] slots = new long[CAPACITY];
        private int count;

        long acquire() {
            if (count > 0) {
                count--;
                long slot = slots[count];
                slots[count] = 0L;
                return slot;
            }
            long address = UNSAFE.allocateMemory(MESSAGE_LAYOUT_SIZE);
            UNSAFE.setMemory(address, MESSAGE_LAYOUT_SIZE, (byte) 0);
            return address;
        }

        void release(long slot) {
            if (count < CAPACITY) {
                slots[count++] = slot;
            } else {
                UNSAFE.freeMemory(slot);
            }
        }
    }

}
