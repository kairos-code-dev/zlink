/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;


import java.util.List;

@FunctionalInterface
public interface RequestCallback {
    void onComplete(RequestResult result, List<Message> parts);
}
