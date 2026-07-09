/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.messaging.Message;

/** Common stage for builders that add message parts. */
public interface MessageBuilderStage<TSubmit> {
    TSubmit message(Message part);
}
