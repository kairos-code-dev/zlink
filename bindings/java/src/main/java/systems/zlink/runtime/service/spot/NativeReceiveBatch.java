/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.ReceiveBatch;
import systems.zlink.contracts.service.spot.ReceiveRecord;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.NativeLayouts;
import systems.zlink.runtime.nativeapi.NativeServiceSymbols;
import systems.zlink.runtime.nativeapi.ServiceInterop;
import systems.zlink.runtime.nativeapi.ServiceLayouts;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.util.Arrays;
import java.util.List;

public final class NativeReceiveBatch implements ReceiveBatch {
    private static final long STRIDE = ServiceLayouts.RECEIVE_RECORD.byteSize();

    private MemorySegment handle;

    NativeReceiveBatch(int messageCapacity, int partCapacity, int byteCapacity) {
        this.handle = NativeServiceSymbols.receiveBatchNew(messageCapacity, partCapacity,
            byteCapacity);
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
        return (int) NativeServiceSymbols.receiveBatchCount(handle);
    }

    @Override
    public ReceiveRecord at(int index) {
        long count = NativeServiceSymbols.receiveBatchCount(handle);
        if (index < 0 || index >= count) {
            throw new IndexOutOfBoundsException(index);
        }
        MemorySegment data = NativeServiceSymbols.receiveBatchData(handle)
            .reinterpret(STRIDE * count);
        return ServiceInterop.receiveRecordFromNative(data.asSlice(index * STRIDE, STRIDE));
    }

    @Override
    public List<Message> retainMessage(int index) {
        ReceiveRecord record = at(index);
        int partCount = record.partCount();
        if (partCount <= 0) {
            return List.of();
        }
        try (Arena arena = Arena.ofConfined()) {
            long msgStride = NativeLayouts.MESSAGE_LAYOUT.byteSize();
            MemorySegment partsOut = arena.allocate(msgStride * partCount);
            MemorySegment countInout = arena.allocate(ValueLayout.JAVA_LONG);
            countInout.set(ValueLayout.JAVA_LONG, 0, partCount);
            int rc = NativeServiceSymbols.receiveBatchRetainMessage(handle, index, partsOut,
                countInout);
            if (rc != MeshCalls.CONFIG_OK) {
                return List.of();
            }
            long got = countInout.get(ValueLayout.JAVA_LONG, 0);
            Message[] messages = InternalAccess.messageFromOwnedMessageVector(partsOut, got);
            return Arrays.asList(messages);
        }
    }

    @Override
    public void reset() {
        NativeServiceSymbols.receiveBatchReset(handle);
    }

    @Override
    public void close() {
        if (handle == null || handle.address() == 0) {
            return;
        }
        NativeServiceSymbols.receiveBatchDestroy(handle);
        handle = MemorySegment.NULL;
    }
}
