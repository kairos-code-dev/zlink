/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.core;

public interface ZlinkThread extends AutoCloseable {
    void join();

    @Override
    void close();
}
