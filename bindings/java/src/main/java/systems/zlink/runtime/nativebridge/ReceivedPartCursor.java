/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.nativebridge;

import systems.zlink.contracts.Message;

public interface ReceivedPartCursor extends AutoCloseable {
    Message nextPartOrNull();

    @Override
    void close();
}
