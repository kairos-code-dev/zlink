/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.eventing;

/** Registration handle returned by {@link ZlinkNativeDispatcher}. */
public interface ZlinkDispatcherRegistration extends AutoCloseable {
    boolean isClosed();

    @Override
    void close();
}
