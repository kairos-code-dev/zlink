/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.sockets;

import java.lang.foreign.MemorySegment;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.Objects;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.errors.ZlinkException;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlag;
import systems.zlink.contracts.sockets.SendResult;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.Native;
import systems.zlink.runtime.nativeapi.NativeLayouts;
import systems.zlink.runtime.nativeapi.NativeMessage;
import systems.zlink.runtime.nativeapi.NativeSubmitErrors;
import systems.zlink.runtime.nativeapi.SendScratch;

final class SocketSendPlane {
    private final NativeSocketRuntime socket;
    private final ThreadLocal<SendScratch> sendScratch =
        ThreadLocal.withInitial(SendScratch::new);

    SocketSendPlane(NativeSocketRuntime socket) {
        this.socket = socket;
    }

    void sendMessageFrame(RoutingId routingId, Message message, SendFlag flag) {
        Objects.requireNonNull(routingId, "routingId");
        Objects.requireNonNull(message, "message");
        Objects.requireNonNull(flag, "flag");
        ensureBlockingSendAllowed(flag);
        while (true) {
            int rc = sendPartOnce(message, routingId, flag.getValue(),
                Native.PART_FINAL);
            if (rc == 0)
                return;
            int errno = Native.errno();
            if (isTransientBlockingSendErrno(errno))
                continue;
            throwPartSubmitFailure("zlink_send_part_rid");
        }
    }

    SendResult sendMessageFrameNoWaitResult(RoutingId routingId, Message message) {
        Objects.requireNonNull(routingId, "routingId");
        Objects.requireNonNull(message, "message");
        while (true) {
            int rc = sendPartOnce(message, routingId,
                SendFlag.DONTWAIT.getValue(), Native.PART_FINAL);
            if (rc == 0)
                return SendResult.SENT;
            int errno = Native.errno();
            if (errno == NativeSocketRuntime.ERRNO_EINTR)
                continue;
            return classifyNonBlockingSendErrno("zlink_send_part_rid");
        }
    }

    boolean send(byte[] routingIdBytes, Message part, SendFlag flags) {
        Objects.requireNonNull(routingIdBytes, "routingIdBytes");
        Objects.requireNonNull(part, "part");
        Objects.requireNonNull(flags, "flags");
        ensureBlockingSendAllowed(flags);
        while (true) {
            int rc = sendPartOnce(part, routingIdBytes, flags.getValue(),
                Native.PART_FINAL);
            if (rc == 0)
                return true;
            int errno = Native.errno();
            if (errno == NativeSocketRuntime.ERRNO_EINTR)
                continue;
            if ((flags.getValue() & SendFlag.DONTWAIT.getValue()) != 0
                && (errno == NativeSocketRuntime.ERRNO_EAGAIN
                    || errno == NativeSocketRuntime.ERRNO_EWOULDBLOCK_WIN)) {
                return false;
            }
            throwPartSubmitFailure("zlink_send_part_rid");
        }
    }

    void send(int rid, Message part, SendFlag flags) {
        Objects.requireNonNull(part, "part");
        Objects.requireNonNull(flags, "flags");
        ensureBlockingSendAllowed(flags);
        int rc = Native.sendMultipartU32(socket.handle(), rid,
            InternalAccess.messageNativeHandle(part), 1, flags.getValue());
        if (rc < 0)
            throw ZlinkException.fromLastError("zlink_java_send_u32");
        InternalAccess.messageMarkTransferred(part);
    }

    int send(int rid, MemorySegment payload, int length, int sendFlags) {
        Objects.requireNonNull(payload, "payload");
        SendFlag flag = SendFlag.fromValue(sendFlags);
        return sendDirectSegment(rid, payload, length, flag);
    }

    int sendCopied(int rid, MemorySegment payload, int length, int sendFlags) {
        Objects.requireNonNull(payload, "payload");
        SendFlag flag = SendFlag.fromValue(sendFlags);
        ensureBlockingSendAllowed(flag);
        SendScratch scratch = sendScratch.get();
        MemorySegment nativeMsg = scratch.nativeMsg;
        int rc = NativeMessage.messageInitSize(nativeMsg, length);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_msg_init_size");
        if (length > 0) {
            MemorySegment dst = NativeMessage.messageData(nativeMsg)
                .reinterpret(length);
            MemorySegment.copy(payload, 0, dst, 0, length);
        }
        boolean success = false;
        try {
            rc = Native.sendMultipartU32(socket.handle(), rid, nativeMsg, 1,
                flag.getValue());
            if (rc < 0)
                throw ZlinkException.fromLastError("zlink_java_send_u32");
            success = true;
        } finally {
            if (!success) {
                try {
                    NativeMessage.messageClose(nativeMsg);
                } catch (RuntimeException ignored) {
                }
            }
        }
        return length;
    }

    void publishMessageFrame(String topicId, Message message, SendFlag flags) {
        Objects.requireNonNull(topicId, "topicId");
        Objects.requireNonNull(message, "message");
        Objects.requireNonNull(flags, "flags");
        ensureBlockingSendAllowed(flags);
        while (true) {
            int rc = publishPartOnce(topicId, message, flags.getValue(),
                Native.PART_FINAL);
            if (rc == 0)
                return;
            int errno = Native.errno();
            if (errno == NativeSocketRuntime.ERRNO_EINTR)
                continue;
            throwPartSubmitFailure("zlink_publish_part");
        }
    }

    SendResult publishMessageFrameNoWaitResult(String topicId, Message message) {
        Objects.requireNonNull(topicId, "topicId");
        Objects.requireNonNull(message, "message");
        while (true) {
            int rc = publishPartOnce(topicId, message,
                SendFlag.DONTWAIT.getValue(), Native.PART_FINAL);
            if (rc == 0)
                return SendResult.SENT;
            int errno = Native.errno();
            if (errno == NativeSocketRuntime.ERRNO_EINTR)
                continue;
            return classifyNonBlockingSendErrno("zlink_publish_part");
        }
    }

    void sendMessageFrame(Message message, SendFlag flag) {
        Objects.requireNonNull(message, "message");
        Objects.requireNonNull(flag, "flag");
        ensureBlockingSendAllowed(flag);
        while (true) {
            int rc = sendPartOnce(message, (RoutingId) null, flag.getValue(),
                Native.PART_FINAL);
            if (rc == 0)
                return;
            int errno = Native.errno();
            if (isTransientBlockingSendErrno(errno))
                continue;
            throwPartSubmitFailure("zlink_send_part");
        }
    }

    boolean sendMessageFrameNoWaitResult(Message message, SendFlag flag) {
        Objects.requireNonNull(message, "message");
        Objects.requireNonNull(flag, "flag");
        while (true) {
            int rc = sendPartOnce(message, (RoutingId) null, flag.getValue(),
                Native.PART_FINAL);
            if (rc == 0)
                return true;
            int errno = Native.errno();
            if (errno == NativeSocketRuntime.ERRNO_EINTR)
                continue;
            if (errno == NativeSocketRuntime.ERRNO_EAGAIN
                || errno == NativeSocketRuntime.ERRNO_EWOULDBLOCK_WIN) {
                return false;
            }
            throw ZlinkException.fromLastError("zlink_send_part");
        }
    }

    SendResult sendMessageFrameNoWaitResult(Message message) {
        Objects.requireNonNull(message, "message");
        while (true) {
            int rc = sendPartOnce(message, (RoutingId) null,
                SendFlag.DONTWAIT.getValue(), Native.PART_FINAL);
            if (rc == 0)
                return SendResult.SENT;
            int errno = Native.errno();
            if (errno == NativeSocketRuntime.ERRNO_EINTR)
                continue;
            return classifyNonBlockingSendErrno("zlink_send_part");
        }
    }

    void sendParts(RoutingId routingId, List<Message> parts,
                   SendFlag flags, boolean nonBlocking) {
        socket.ensureOpen();
        validateParts(parts);
        ensureBlockingSendAllowed(flags);
        boolean explicitNonBlocking =
            (flags.getValue() & SendFlag.DONTWAIT.getValue()) != 0;
        for (int i = 0; i < parts.size(); i++) {
            int partFlag = i + 1 < parts.size()
                ? Native.PART_MORE : Native.PART_FINAL;
            while (true) {
                int rc = sendPartOnce(parts.get(i), routingId, flags.getValue(),
                    partFlag);
                if (rc == 0)
                    break;
                int errno = Native.errno();
                if (errno == NativeSocketRuntime.ERRNO_EINTR)
                    continue;
                if ((nonBlocking || explicitNonBlocking)
                    && (errno == NativeSocketRuntime.ERRNO_EAGAIN
                        || errno == NativeSocketRuntime.ERRNO_EWOULDBLOCK_WIN)) {
                    throw new ZlinkSubmitException(SubmitResult.BACKPRESSURED,
                        errno);
                }
                throwPartSubmitFailure(
                    routingId == null ? "zlink_send_part"
                        : "zlink_send_part_rid");
            }
        }
    }

    SendResult sendNoWaitPartsResult(RoutingId routingId, List<Message> parts) {
        socket.ensureOpen();
        validateParts(parts);
        for (int i = 0; i < parts.size(); i++) {
            int partFlag = i + 1 < parts.size()
                ? Native.PART_MORE : Native.PART_FINAL;
            while (true) {
                int rc = sendPartOnce(parts.get(i), routingId,
                    SendFlag.DONTWAIT.getValue(), partFlag);
                if (rc == 0)
                    break;
                int errno = Native.errno();
                if (errno == NativeSocketRuntime.ERRNO_EINTR)
                    continue;
                return classifyNonBlockingSendErrno(
                    routingId == null ? "zlink_send_part"
                        : "zlink_send_part_rid");
            }
        }
        return SendResult.SENT;
    }

    void publishParts(String topicId, List<Message> parts,
                      SendFlag flags, boolean nonBlocking) {
        socket.ensureOpen();
        validateParts(parts);
        ensureBlockingSendAllowed(flags);
        boolean explicitNonBlocking =
            (flags.getValue() & SendFlag.DONTWAIT.getValue()) != 0;
        MemorySegment nativeTopic = nativeTopic(sendScratch.get(), topicId);
        for (int i = 0; i < parts.size(); i++) {
            int partFlag = i + 1 < parts.size()
                ? Native.PART_MORE : Native.PART_FINAL;
            while (true) {
                int rc = publishPartOnce(nativeTopic, parts.get(i),
                    flags.getValue(), partFlag);
                if (rc == 0)
                    break;
                int errno = Native.errno();
                if (errno == NativeSocketRuntime.ERRNO_EINTR)
                    continue;
                if ((nonBlocking || explicitNonBlocking)
                    && (errno == NativeSocketRuntime.ERRNO_EAGAIN
                        || errno == NativeSocketRuntime.ERRNO_EWOULDBLOCK_WIN)) {
                    throw new ZlinkSubmitException(SubmitResult.BACKPRESSURED,
                        errno);
                }
                throwPartSubmitFailure("zlink_publish_part");
            }
        }
    }

    SendResult publishNoWaitPartsResult(String topicId, List<Message> parts) {
        socket.ensureOpen();
        validateParts(parts);
        MemorySegment nativeTopic = nativeTopic(sendScratch.get(), topicId);
        for (int i = 0; i < parts.size(); i++) {
            int partFlag = i + 1 < parts.size()
                ? Native.PART_MORE : Native.PART_FINAL;
            while (true) {
                int rc = publishPartOnce(nativeTopic, parts.get(i),
                    SendFlag.DONTWAIT.getValue(), partFlag);
                if (rc == 0)
                    break;
                int errno = Native.errno();
                if (errno == NativeSocketRuntime.ERRNO_EINTR)
                    continue;
                return classifyNonBlockingSendErrno("zlink_publish_part");
            }
        }
        return SendResult.SENT;
    }

    private int sendDirectSegment(int rid, MemorySegment payload, int length,
                                  SendFlag flag) {
        return sendCopied(rid, payload, length, flag.getValue());
    }

    private int sendPartOnce(Message message, RoutingId routingId, int flags,
                             int partFlag) {
        SendScratch scratch = sendScratch.get();
        MemorySegment messageHandle = InternalAccess.messageNativeHandle(message);
        MemorySegment nativeRoutingId = routingId == null
            ? MemorySegment.NULL
            : nativeRoutingId(scratch, routingId);
        boolean useCritical =
            (flags & SendFlag.DONTWAIT.getValue()) != 0;
        int rc;
        if (nativeRoutingId.address() == 0) {
            rc = useCritical
                ? Native.sendPartNoWaitCritical(socket.handle(), messageHandle,
                    flags, partFlag)
                : Native.sendPart(socket.handle(), messageHandle, flags,
                    partFlag);
        } else {
            rc = useCritical
                ? Native.sendPartRidNoWaitCritical(socket.handle(),
                    nativeRoutingId, messageHandle, flags, partFlag)
                : Native.sendPartRid(socket.handle(), nativeRoutingId,
                    messageHandle, flags, partFlag);
        }
        if (rc == 0) {
            InternalAccess.messageMarkTransferred(message);
        }
        return rc;
    }

    private int sendPartOnce(Message message, byte[] routingIdBytes, int flags,
                             int partFlag) {
        SendScratch scratch = sendScratch.get();
        MemorySegment messageHandle = InternalAccess.messageNativeHandle(message);
        MemorySegment nativeRoutingId = nativeRoutingId(scratch,
            routingIdBytes);
        boolean useCritical =
            (flags & SendFlag.DONTWAIT.getValue()) != 0;
        int rc = useCritical
            ? Native.sendPartRidNoWaitCritical(socket.handle(), nativeRoutingId,
                messageHandle, flags, partFlag)
            : Native.sendPartRid(socket.handle(), nativeRoutingId, messageHandle,
                flags, partFlag);
        if (rc == 0) {
            InternalAccess.messageMarkTransferred(message);
        }
        return rc;
    }

    private int publishPartOnce(String topicId, Message message, int flags,
                                int partFlag) {
        SendScratch scratch = sendScratch.get();
        MemorySegment nativeTopic = nativeTopic(scratch, topicId);
        MemorySegment messageHandle = InternalAccess.messageNativeHandle(message);
        boolean useCritical =
            (flags & SendFlag.DONTWAIT.getValue()) != 0;
        int rc = useCritical
            ? Native.publishPartNoWaitCritical(socket.handle(), nativeTopic,
                messageHandle, flags, partFlag)
            : Native.publishPart(socket.handle(), nativeTopic, messageHandle,
                flags, partFlag);
        if (rc == 0) {
            InternalAccess.messageMarkTransferred(message);
        }
        return rc;
    }

    private int publishPartOnce(MemorySegment nativeTopic, Message message,
                                int flags, int partFlag) {
        MemorySegment messageHandle = InternalAccess.messageNativeHandle(message);
        boolean useCritical =
            (flags & SendFlag.DONTWAIT.getValue()) != 0;
        int rc = useCritical
            ? Native.publishPartNoWaitCritical(socket.handle(), nativeTopic,
                messageHandle, flags, partFlag)
            : Native.publishPart(socket.handle(), nativeTopic, messageHandle,
                flags, partFlag);
        if (rc == 0) {
            InternalAccess.messageMarkTransferred(message);
        }
        return rc;
    }

    private static MemorySegment nativeTopic(SendScratch scratch,
                                             String topicId) {
        if (scratch.cachedTopicString != null
            && scratch.cachedTopicString.equals(topicId)
            && scratch.cachedTopicSegment != null) {
            return scratch.cachedTopicSegment;
        }
        MemorySegment encoded = scratch.arena.allocateFrom(topicId,
            StandardCharsets.UTF_8);
        scratch.cachedTopicString = topicId;
        scratch.cachedTopicSegment = encoded;
        return encoded;
    }

    private SendResult classifyNonBlockingSendErrno(String apiName) {
        int errno = Native.errno();
        if (NativeSubmitErrors.isBackpressured(errno))
            return SendResult.BACKPRESSURED;
        if (NativeSubmitErrors.isNotConnected(errno))
            return SendResult.NOT_READY;
        if (NativeSubmitErrors.isNotAdmitted(errno)) {
            throw new ZlinkSubmitException(SubmitResult.NOT_ADMITTED, errno);
        }
        throw ZlinkException.fromLastError(apiName);
    }

    private void throwPartSubmitFailure(String apiName) {
        int errno = Native.errno();
        ZlinkSubmitException submit = NativeSubmitErrors.submitExceptionOrNull(errno);
        if (submit != null)
            throw submit;
        throw ZlinkException.fromLastError(apiName);
    }

    private static void validateParts(List<Message> parts) {
        if (parts.isEmpty())
            throw new IllegalArgumentException("parts must not be empty");
        for (int i = 0; i < parts.size(); i++) {
            if (parts.get(i) == null)
                throw new IllegalArgumentException("parts[" + i + "] is null");
        }
    }

    private static void ensureBlockingSendAllowed(SendFlag flags) {
        Objects.requireNonNull(flags, "flags");
        if (InternalAccess.inCallback()
            && (flags.getValue() & SendFlag.DONTWAIT.getValue()) == 0) {
            throw new IllegalStateException(
                "blocking send is not supported from callback context; use SendFlag.DONTWAIT");
        }
    }

    private static MemorySegment nativeRoutingId(SendScratch scratch,
                                                 RoutingId routingId) {
        byte[] value = InternalAccess.routingIdTrustedBytes(routingId);
        if (scratch.cachedRoutingIdBytes == value) {
            return scratch.cachedRoutingIdSegment;
        }
        MemorySegment cachedRid = cachedNativeRoutingId(scratch, value);
        if (cachedRid != null) {
            scratch.cachedRoutingIdBytes = value;
            scratch.cachedRoutingIdSegment = cachedRid;
            return cachedRid;
        }
        MemorySegment nativeRid = scratch.nativeRoutingId;
        writeNativeRoutingId(nativeRid, value);
        scratch.cachedRoutingIdBytes = value;
        scratch.cachedRoutingIdSegment = nativeRid;
        return nativeRid;
    }

    private static MemorySegment nativeRoutingId(SendScratch scratch,
                                                 byte[] value) {
        MemorySegment nativeRid = scratch.nativeRoutingId;
        writeNativeRoutingId(nativeRid, value);
        return nativeRid;
    }

    private static MemorySegment cachedNativeRoutingId(SendScratch scratch,
                                                       byte[] value) {
        byte[][] keys = scratch.cachedRoutingIdKeys;
        MemorySegment[] segments = scratch.cachedRoutingIdSegments;
        if (keys == null || segments == null) {
            keys = new byte[SendScratch.ROUTING_ID_CACHE_CAPACITY][];
            segments = new MemorySegment[SendScratch.ROUTING_ID_CACHE_CAPACITY];
            scratch.cachedRoutingIdKeys = keys;
            scratch.cachedRoutingIdSegments = segments;
        }
        int slot = System.identityHashCode(value)
            & (SendScratch.ROUTING_ID_CACHE_CAPACITY - 1);
        MemorySegment nativeRid = segments[slot];
        if (keys[slot] == value && nativeRid != null) {
            return nativeRid;
        }
        if (nativeRid == null) {
            nativeRid = scratch.arena.allocate(NativeLayouts.ROUTING_ID_LAYOUT);
            segments[slot] = nativeRid;
        }
        keys[slot] = value;
        writeNativeRoutingId(nativeRid, value);
        return nativeRid;
    }

    private static void writeNativeRoutingId(MemorySegment nativeRid,
                                             byte[] value) {
        nativeRid.set(java.lang.foreign.ValueLayout.JAVA_BYTE,
            NativeLayouts.ROUTING_ID_SIZE_OFFSET, (byte) value.length);
        if (value.length > 0) {
            MemorySegment.copy(MemorySegment.ofArray(value), 0, nativeRid,
                NativeLayouts.ROUTING_ID_DATA_OFFSET, value.length);
        }
    }

    private static boolean isTransientBlockingSendErrno(int errno) {
        return errno == NativeSocketRuntime.ERRNO_EINTR
            || errno == NativeSocketRuntime.ERRNO_EAGAIN
            || errno == NativeSocketRuntime.ERRNO_EWOULDBLOCK_WIN
            || errno == NativeSocketRuntime.ERRNO_ENOTCONN
            || errno == NativeSocketRuntime.ERRNO_ENOTCONN_WIN
            || errno == NativeSocketRuntime.ERRNO_EHOSTUNREACH
            || errno == NativeSocketRuntime.ERRNO_EHOSTUNREACH_WIN;
    }
}
