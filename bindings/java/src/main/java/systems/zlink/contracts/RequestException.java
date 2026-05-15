/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;


public final class RequestException extends ZlinkException {
    private final RequestResult result;

    public RequestException(RequestResult result) {
        this(result, 0);
    }

    public RequestException(RequestResult result, int internalErrno) {
        super(result.value(), internalErrno);
        this.result = result;
    }

    public RequestResult getResult() {
        return result;
    }
}
