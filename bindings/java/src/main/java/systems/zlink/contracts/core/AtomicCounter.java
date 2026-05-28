/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.core;

public interface AtomicCounter extends AutoCloseable {
    void set(int value);

    int increment();

    int decrement();

    int value();

    @Override
    void close();
}
