/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import systems.zlink.contracts.service.spot.Claim;
import systems.zlink.contracts.service.spot.ReadyBatch;
import systems.zlink.contracts.service.spot.ReadyRecord;
import systems.zlink.runtime.nativeapi.NativeServiceSymbols;
import systems.zlink.runtime.nativeapi.ServiceInterop;
import systems.zlink.runtime.nativeapi.ServiceLayouts;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;

public final class NativeReadyBatch implements ReadyBatch {
    private static final long STRIDE = ServiceLayouts.READY_RECORD.byteSize();

    private MemorySegment handle;

    NativeReadyBatch(int recordCapacity) {
        this.handle = NativeServiceSymbols.readyBatchNew(recordCapacity);
        if (handle == null || handle.address() == 0) {
            throw systems.zlink.contracts.errors.ZlinkException.fromLastError(
                systems.zlink.contracts.errors.ErrorCategory.CONFIG);
        }
    }

    MemorySegment handle() {
        return handle;
    }

    @Override
    public int count() {
        return (int) NativeServiceSymbols.readyBatchCount(handle);
    }

    @Override
    public ReadyRecord at(int index) {
        long count = NativeServiceSymbols.readyBatchCount(handle);
        if (index < 0 || index >= count) {
            throw new IndexOutOfBoundsException(index);
        }
        MemorySegment data = NativeServiceSymbols.readyBatchData(handle)
            .reinterpret(STRIDE * count);
        return ServiceInterop.readyRecordFromNative(data.asSlice(index * STRIDE, STRIDE));
    }

    @Override
    public void reset() {
        NativeServiceSymbols.readyBatchReset(handle);
    }

    @Override
    public Claim takeClaim(int index) {
        MemorySegment claimSeg = Arena.ofAuto().allocate(ServiceLayouts.CLAIM);
        int rc = NativeServiceSymbols.readyBatchTakeClaim(handle, index, claimSeg);
        return new NativeClaim(claimSeg, rc == MeshCalls.CONFIG_OK);
    }

    @Override
    public void close() {
        if (handle == null || handle.address() == 0) {
            return;
        }
        NativeServiceSymbols.readyBatchDestroy(handle);
        handle = MemorySegment.NULL;
    }
}
