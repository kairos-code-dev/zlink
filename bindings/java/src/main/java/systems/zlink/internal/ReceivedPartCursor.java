/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.internal;

import systems.zlink.Message;

public interface ReceivedPartCursor extends AutoCloseable {
    Message nextPartOrNull();

    @Override
    void close();
}
