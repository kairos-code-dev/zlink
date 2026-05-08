/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.service.spot;

import systems.zlink.Message;

public interface RequestOp {
    RequestSubmitOp message(Message part);
}
