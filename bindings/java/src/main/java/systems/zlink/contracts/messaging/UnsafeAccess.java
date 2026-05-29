/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.messaging;

import java.lang.reflect.Field;
import sun.misc.Unsafe;

final class UnsafeAccess {
    private static final Unsafe UNSAFE = lookupUnsafe();

    private UnsafeAccess() {
    }

    static Unsafe get() {
        return UNSAFE;
    }

    private static Unsafe lookupUnsafe() {
        try {
            Field field = Unsafe.class.getDeclaredField("theUnsafe");
            field.setAccessible(true);
            return (Unsafe) field.get(null);
        } catch (ReflectiveOperationException ex) {
            throw new IllegalStateException("Unable to access sun.misc.Unsafe",
                ex);
        }
    }
}
