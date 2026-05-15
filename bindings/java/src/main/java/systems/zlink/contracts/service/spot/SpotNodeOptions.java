/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;


record SpotNodeOptions(SpotNodeMode mode) {
    SpotNodeOptions {
        if (mode == null)
            mode = SpotNodeMode.ALL;
    }
}
