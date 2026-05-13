/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.service.spot;

import systems.zlink.Message;
import systems.zlink.RequestResult;
import java.util.List;

/** Callback for payload-less Actor operation builders. */
@FunctionalInterface
public interface ReplyHandler {
    void onReply(RequestResult result, List<Message> parts);
}
