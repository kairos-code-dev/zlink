/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import systems.zlink.contracts.Message;
import systems.zlink.contracts.RequestResult;
import java.util.List;

/** Callback for payload-less Actor operation builders. */
@FunctionalInterface
public interface ReplyHandler {
    void onReply(RequestResult result, List<Message> parts);
}
