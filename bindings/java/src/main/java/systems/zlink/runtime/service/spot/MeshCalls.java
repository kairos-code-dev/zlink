/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import systems.zlink.contracts.errors.ConfigResult;
import systems.zlink.contracts.errors.ConnectResult;
import systems.zlink.contracts.errors.ZlinkConfigException;
import systems.zlink.contracts.errors.ZlinkConnectException;
import systems.zlink.contracts.errors.ZlinkRequestException;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.OperationId;
import systems.zlink.contracts.sockets.RequestResult;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.runtime.nativeapi.MessagePartsBuffer;
import systems.zlink.runtime.nativeapi.Native;
import systems.zlink.runtime.nativeapi.NativeSubmitErrors;
import systems.zlink.runtime.nativeapi.ServiceInterop;
import systems.zlink.runtime.nativeapi.ServiceLayouts;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.time.Duration;
import java.util.List;
import java.util.Objects;

/** Shared marshalling/result helpers for the runtime service classes. */
final class MeshCalls {
    static final int SUBMIT_OK = SubmitResult.OK.value();
    static final int CONFIG_OK = ConfigResult.OK.value();
    static final int CONFIG_NOT_FOUND = ConfigResult.NOT_FOUND.value();

    private MeshCalls() {
    }

    static int timeout(Duration timeout) {
        if (timeout == null) {
            return 0;
        }
        long ms = timeout.toMillis();
        if (ms < 0) {
            return 0;
        }
        if (ms > 0xFFFF_FFFFL) {
            return -1;
        }
        return (int) ms;
    }

    static long count(List<Message> parts) {
        return parts == null ? 0L : parts.size();
    }

    static MemorySegment parts(Arena arena, List<Message> parts) {
        if (parts == null || parts.isEmpty()) {
            return MemorySegment.NULL;
        }
        MessagePartsBuffer buffer = new MessagePartsBuffer();
        for (int i = 0; i < parts.size(); i++) {
            buffer.add(Objects.requireNonNull(parts.get(i), "parts[" + i + "]"));
        }
        return buffer.copyToNativeArray(arena);
    }

    static MemorySegment newOperationId(Arena arena) {
        return arena.allocate(ServiceLayouts.OPERATION_ID);
    }

    /** Verifies a submit result; on failure frees the native parts and throws. */
    static void submitOk(int rc, MemorySegment nativeParts, long partCount, String api) {
        if (rc == SUBMIT_OK) {
            return;
        }
        MessagePartsBuffer.closeNativeArray(nativeParts, (int) partCount);
        throw submitFailure(rc);
    }

    static ZlinkSubmitException submitFailure(int rc) {
        int errno = Native.errno();
        ZlinkSubmitException byErrno = NativeSubmitErrors.submitExceptionOrNull(errno);
        if (byErrno != null) {
            return byErrno;
        }
        return new ZlinkSubmitException(SubmitResult.fromValue(rc), errno);
    }

    static void configOk(int rc) {
        if (rc != CONFIG_OK) {
            throw new ZlinkConfigException(ConfigResult.fromValue(rc), Native.errno());
        }
    }

    static void requestOk(int rc) {
        if (rc != RequestResult.OK.value()) {
            throw new ZlinkRequestException(RequestResult.fromValue(rc), Native.errno());
        }
    }

    static void connectOk(int rc) {
        if (rc != ConnectResult.OK.value()) {
            throw new ZlinkConnectException(ConnectResult.fromValue(rc), Native.errno());
        }
    }

    static OperationId operationId(MemorySegment opidSeg) {
        return ServiceInterop.operationIdFromNative(opidSeg);
    }
}
