/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;


public final class BindException extends ZlinkException {
    private final BindResult result;

    public BindException(BindResult result) {
        this(result, 0);
    }

    public BindException(BindResult result, int internalErrno) {
        super(result.value(), internalErrno);
        this.result = result;
    }

    public BindResult getResult() {
        return result;
    }
}
