/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.core;

import java.time.Duration;

public interface ZlinkStopwatch extends AutoCloseable {
    Duration intermediate();

    Duration stop();

    @Override
    void close();
}
