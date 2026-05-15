/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;


public final class SubmitException extends ZlinkException {
    private final SubmitResult result;

    public SubmitException(SubmitResult result) {
        this(result, 0);
    }

    public SubmitException(SubmitResult result, int internalErrno) {
        super(result.value(), internalErrno);
        this.result = result;
    }

    public SubmitResult getResult() {
        return result;
    }
}
