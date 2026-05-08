/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.internal;

import dev.kairoscode.zlink.Message;

public interface ReceivedPartCursor extends AutoCloseable {
    Message nextPartOrNull();

    @Override
    void close();
}
