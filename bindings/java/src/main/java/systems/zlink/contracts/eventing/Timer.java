/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.eventing;

import java.time.Duration;

public interface Timer extends AutoCloseable {

    public abstract void start(Duration interval, long repeatCount);

    public abstract void stop();

    public abstract long recv();

    public abstract void onFire(TimerHandler handler);

    @Override
    public abstract void close();
}
