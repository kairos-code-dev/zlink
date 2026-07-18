/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import systems.zlink.contracts.service.spot.Claim;
import systems.zlink.contracts.service.spot.ClaimRecvResult;
import systems.zlink.contracts.service.spot.ReceiveBatch;
import systems.zlink.contracts.service.spot.ReceiveRequirements;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.runtime.nativeapi.NativeServiceSymbols;
import systems.zlink.runtime.nativeapi.ServiceInterop;
import systems.zlink.runtime.nativeapi.ServiceLayouts;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.Objects;

public final class NativeClaim implements Claim {
    private static final int RECV_OK = 0;

    private final MemorySegment claim;
    private boolean valid;

    NativeClaim(MemorySegment claim, boolean valid) {
        this.claim = claim;
        this.valid = valid;
    }

    @Override
    public boolean valid() {
        return valid;
    }

    @Override
    public ClaimRecvResult recvBatch(ReceiveBatch batch, RecvFlags flags) {
        Objects.requireNonNull(batch, "batch");
        if (!valid) {
            return new ClaimRecvResult(-1, new ReceiveRequirements(0, 0, 0));
        }
        NativeReceiveBatch nativeBatch = (NativeReceiveBatch) batch;
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment required = ServiceInterop.allocStamped(arena,
                ServiceLayouts.RECEIVE_REQUIREMENTS);
            int rc = NativeServiceSymbols.claimRecvBatch(claim, nativeBatch.handle(), required,
                flags.value());
            if (rc == RECV_OK) {
                valid = false;
            }
            return new ClaimRecvResult(rc, ServiceInterop.receiveRequirementsFromNative(required));
        }
    }

    @Override
    public void release() {
        if (valid) {
            NativeServiceSymbols.claimRelease(claim);
            valid = false;
        }
    }

    @Override
    public void close() {
        release();
    }
}
