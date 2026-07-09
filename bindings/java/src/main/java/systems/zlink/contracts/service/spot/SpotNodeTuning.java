/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.sockets.AutoHwmProfile;

/** Runtime tuning options exposed by a spot node. */
public interface SpotNodeTuning {
    AutoHwmProfile routerHwmProfile();

    void routerHwmProfile(AutoHwmProfile profile);

    int routerHighWaterMark();

    void routerHighWaterMark(int value);

    AutoHwmProfile pubSubHwmProfile();

    void pubSubHwmProfile(AutoHwmProfile profile);

    int pubSubHighWaterMark();

    void pubSubHighWaterMark(int value);

    int dispatchWorkersMin();

    void dispatchWorkersMin(int value);

    int dispatchWorkersMax();

    void dispatchWorkersMax(int value);
}
