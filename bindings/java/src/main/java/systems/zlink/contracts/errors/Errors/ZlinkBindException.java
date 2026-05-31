/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.errors;


/** Thrown when binding a socket to an endpoint fails. */
public final class ZlinkBindException extends ZlinkException {
    private final BindResult result;

    public ZlinkBindException(BindResult result) {
        this(result, 0);
    }

    public ZlinkBindException(BindResult result, int internalErrno) {
        super(result.value(), internalErrno);
        this.result = result;
    }

    public BindResult getResult() {
        return result;
    }
}
