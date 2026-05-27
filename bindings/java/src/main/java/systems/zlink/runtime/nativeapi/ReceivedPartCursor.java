/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.nativeapi;

import systems.zlink.contracts.messaging.Message;
public interface ReceivedPartCursor extends AutoCloseable {
    Message nextPartOrNull();

    @Override
    void close();
}
