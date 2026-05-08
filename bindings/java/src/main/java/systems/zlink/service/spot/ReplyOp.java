/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.service.spot;

import systems.zlink.Message;

public interface ReplyOp {
    ReplySubmitOp message(Message part);
}
