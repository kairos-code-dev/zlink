/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;


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
