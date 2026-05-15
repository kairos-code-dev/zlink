/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;


public final class HandlerException extends ZlinkException {
    private final HandlerResult result;

    public HandlerException(HandlerResult result) {
        this(result, 0);
    }

    public HandlerException(HandlerResult result, int internalErrno) {
        super(result.value(), internalErrno);
        this.result = result;
    }

    public HandlerResult getResult() {
        return result;
    }
}
