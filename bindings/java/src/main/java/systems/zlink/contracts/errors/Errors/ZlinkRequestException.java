/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.errors;

import systems.zlink.contracts.sockets.RequestResult;
/** Thrown when a request fails or its reply reports an error. */
public final class ZlinkRequestException extends ZlinkException {
    private final RequestResult result;

    public ZlinkRequestException(RequestResult result) {
        this(result, 0);
    }

    public ZlinkRequestException(RequestResult result, int internalErrno) {
        super(result.value(), internalErrno);
        this.result = result;
    }

    public RequestResult getResult() {
        return result;
    }
}
