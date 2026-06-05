/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.errors;


/** Thrown when binding a socket to an endpoint fails. */
public final class ZlinkBindException extends ZlinkException {
    private final BindResult result;

    public ZlinkBindException(BindResult result) {
        this(result, 0);
    }

    public ZlinkBindException(BindResult result, int nativeErrno) {
        super(result.value(), nativeErrno);
        this.result = result;
    }

    public BindResult getResult() {
        return result;
    }
}
