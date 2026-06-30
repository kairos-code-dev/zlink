// SPDX-License-Identifier: MPL-2.0

using System.Runtime.CompilerServices;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink.Runtime.Sockets.Internal;

internal sealed partial class SocketKernel
{
    private void SendSingleCore(Message message, int flags)
    {
        ZlinkMsg nativePart = default;
        var shouldRestore = false;
        try
        {
            message.MoveTo(ref nativePart);
            shouldRestore = true;
            var rc = NativeMethods.zlink_send_part(Handle, ref nativePart, flags,
                NativeMethods.ZlinkPartFlag.Final);
            if (rc == 0)
            {
                shouldRestore = false;
                return;
            }

            var errno = NativeMethods.zlink_errno();
            message.RestoreFrom(ref nativePart);
            shouldRestore = false;
            throw ZlinkException.CreateSubmitException(errno);
        }
        catch
        {
            if (shouldRestore)
                message.RestoreFrom(ref nativePart);
            throw;
        }
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private SendResult SendSingleResultCore(Message message, int flags)
    {
        ZlinkMsg nativePart = default;
        var shouldRestore = false;
        try
        {
            message.MoveTo(ref nativePart);
            shouldRestore = true;
            var rc = NativeMethods.zlink_send_part(Handle, ref nativePart, flags,
                NativeMethods.ZlinkPartFlag.Final);
            if (rc == 0)
            {
                shouldRestore = false;
                return SendResult.Sent;
            }

            var errno = NativeMethods.zlink_errno();
            message.RestoreFrom(ref nativePart);
            shouldRestore = false;
            var mappedResult = SendResultErrno.TryMap(errno);
            if (mappedResult == null)
                throw ZlinkException.CreateSubmitException(errno);
            return mappedResult.Value;
        }
        catch
        {
            if (shouldRestore)
                message.RestoreFrom(ref nativePart);
            throw;
        }
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private SendResult SendSingleNoWaitResultCore(Message message)
    {
        ZlinkMsg nativePart = default;
        var shouldRestore = false;
        try
        {
            message.MoveTo(ref nativePart);
            shouldRestore = true;
            // DONT_WAIT-only critical variant: contractually non-blocking.
            var rc = NativeMethods.zlink_send_part_nowait(Handle, ref nativePart,
                DontWaitFlag, NativeMethods.ZlinkPartFlag.Final);
            if (rc == 0)
            {
                shouldRestore = false;
                return SendResult.Sent;
            }

            var errno = NativeMethods.zlink_errno();
            message.RestoreFrom(ref nativePart);
            shouldRestore = false;
            var mappedResult = SendResultErrno.TryMap(errno);
            if (mappedResult == null)
                throw ZlinkException.CreateSubmitException(errno);
            return mappedResult.Value;
        }
        catch
        {
            if (shouldRestore)
                message.RestoreFrom(ref nativePart);
            throw;
        }
    }

    private void PublishSingleCore(string topic, Message message,
        int flags)
    {
        PublishSingleCore(GetPublishTopicUtf8(topic), message, flags);
    }

    private unsafe void PublishSingleCore(byte[] topicUtf8, Message message,
        int flags)
    {
        ZlinkMsg nativePart = default;
        var shouldRestore = false;
        try
        {
            message.MoveTo(ref nativePart);
            shouldRestore = true;
            fixed (byte* topicPtr = topicUtf8)
            {
                var rc = NativeMethods.zlink_publish_part_utf8(Handle,
                    topicPtr, ref nativePart, flags,
                    NativeMethods.ZlinkPartFlag.Final);
                if (rc == 0)
                {
                    shouldRestore = false;
                    return;
                }

                var errno = NativeMethods.zlink_errno();
                message.RestoreFrom(ref nativePart);
                shouldRestore = false;
                throw ZlinkException.CreateSubmitException(errno);
            }
        }
        catch
        {
            if (shouldRestore)
                message.RestoreFrom(ref nativePart);
            throw;
        }
    }

    private SendResult PublishNoWaitSingleCore(string topic, Message message)
    {
        return PublishNoWaitSingleCore(GetPublishTopicUtf8(topic), message);
    }

    private unsafe SendResult PublishNoWaitSingleCore(byte[] topicUtf8,
        Message message)
    {
        ZlinkMsg nativePart = default;
        var shouldRestore = false;
        try
        {
            message.MoveTo(ref nativePart);
            shouldRestore = true;
            fixed (byte* topicPtr = topicUtf8)
            {
                var rc = NativeMethods.zlink_publish_part_utf8(Handle,
                    topicPtr, ref nativePart, DontWaitFlag,
                    NativeMethods.ZlinkPartFlag.Final);
                if (rc == 0)
                {
                    shouldRestore = false;
                    return SendResult.Sent;
                }
            }

            var errno = NativeMethods.zlink_errno();
            message.RestoreFrom(ref nativePart);
            shouldRestore = false;
            var sendResult = SendResultErrno.TryMap(errno);
            if (sendResult == null)
                throw ZlinkException.CreateSubmitException(errno);
            return sendResult.Value;
        }
        catch
        {
            if (shouldRestore)
                message.RestoreFrom(ref nativePart);
            throw;
        }
    }

    private byte[] GetPublishTopicUtf8(string topic)
    {
        var cached = _publishTopicCacheUtf8;
        var cachedKey = _publishTopicCacheKey;
        if (cached != null
            && (ReferenceEquals(cachedKey, topic)
                || string.Equals(cachedKey, topic, StringComparison.Ordinal)))
            return cached;

        var encoded = PublishTopicEncoding.GetNullTerminatedUtf8(topic);
        _publishTopicCacheKey = topic;
        _publishTopicCacheUtf8 = encoded;
        return encoded;
    }

    private byte[] GetValidatedPublishTopicUtf8(string topic, string paramName)
    {
        var cached = _publishTopicCacheUtf8;
        var cachedKey = _publishTopicCacheKey;
        if (cached != null
            && (ReferenceEquals(cachedKey, topic)
                || string.Equals(cachedKey, topic, StringComparison.Ordinal)))
            return cached;

        BoundaryValidation.ValidateTopicOrFilterUtf8(topic, paramName);
        var encoded = PublishTopicEncoding.GetNullTerminatedUtf8(topic);
        _publishTopicCacheKey = topic;
        _publishTopicCacheUtf8 = encoded;
        return encoded;
    }

    private void SendSingleCore(ref ZlinkRoutingId routingId,
        Message message, int flags)
    {
        ZlinkMsg nativePart = default;
        var shouldRestore = false;
        try
        {
            message.MoveTo(ref nativePart);
            shouldRestore = true;
            var rc = (flags & DontWaitFlag) != 0
                ? NativeMethods.zlink_send_part_rid_nowait(Handle,
                    ref routingId, ref nativePart, flags,
                    NativeMethods.ZlinkPartFlag.Final)
                : NativeMethods.zlink_send_part_rid(Handle, ref routingId,
                    ref nativePart, flags, NativeMethods.ZlinkPartFlag.Final);
            if (rc == 0)
            {
                shouldRestore = false;
                return;
            }

            var errno = NativeMethods.zlink_errno();
            message.RestoreFrom(ref nativePart);
            shouldRestore = false;
            throw ZlinkException.CreateSubmitException(errno);
        }
        catch
        {
            if (shouldRestore)
                message.RestoreFrom(ref nativePart);
            throw;
        }
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private SendResult SendSingleResultCore(ref ZlinkRoutingId routingId,
        Message message, int flags)
    {
        ZlinkMsg nativePart = default;
        var shouldRestore = false;
        try
        {
            message.MoveTo(ref nativePart);
            shouldRestore = true;
            var rc = (flags & DontWaitFlag) != 0
                ? NativeMethods.zlink_send_part_rid_nowait(Handle,
                    ref routingId, ref nativePart, flags,
                    NativeMethods.ZlinkPartFlag.Final)
                : NativeMethods.zlink_send_part_rid(Handle, ref routingId,
                    ref nativePart, flags, NativeMethods.ZlinkPartFlag.Final);
            if (rc == 0)
            {
                shouldRestore = false;
                return SendResult.Sent;
            }

            var errno = NativeMethods.zlink_errno();
            message.RestoreFrom(ref nativePart);
            shouldRestore = false;
            var mappedResult = SendResultErrno.TryMap(errno);
            if (mappedResult == null)
                throw ZlinkException.CreateSubmitException(errno);
            return mappedResult.Value;
        }
        catch
        {
            if (shouldRestore)
                message.RestoreFrom(ref nativePart);
            throw;
        }
    }

    private SendResult SendSingleNoWaitResultCore(ref ZlinkRoutingId routingId,
        Message message)
    {
        ZlinkMsg nativePart = default;
        var shouldRestore = false;
        try
        {
            message.MoveTo(ref nativePart);
            shouldRestore = true;
            var rc = NativeMethods.zlink_send_part_rid_nowait(Handle,
                ref routingId, ref nativePart, DontWaitFlag,
                NativeMethods.ZlinkPartFlag.Final);
            if (rc == 0)
            {
                shouldRestore = false;
                return SendResult.Sent;
            }

            var errno = NativeMethods.zlink_errno();
            message.RestoreFrom(ref nativePart);
            shouldRestore = false;
            var mappedResult = SendResultErrno.TryMap(errno);
            if (mappedResult == null)
                throw ZlinkException.CreateSubmitException(errno);
            return mappedResult.Value;
        }
        catch
        {
            if (shouldRestore)
                message.RestoreFrom(ref nativePart);
            throw;
        }
    }
}