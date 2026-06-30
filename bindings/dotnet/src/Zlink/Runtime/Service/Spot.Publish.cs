// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;
using Systems.Zlink.Runtime.Native;
using Systems.Zlink.Runtime.Sockets.Internal;

namespace Systems.Zlink;

internal sealed partial class Spot
{
    internal bool Publish(string topic, Message message,
        SendFlags flags = SendFlags.None)
    {
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        EnsureNotDisposed();
        var topicUtf8 = GetValidatedPublishTopicUtf8(topic,
            nameof(topic));
        if ((flags & SendFlags.DontWait) != 0)
            return SocketKernel.TrySendOrThrow(PublishNoWaitSingleCore(topicUtf8,
                message));

        PublishSingleCore(topicUtf8, message, (int)flags);
        return true;
    }

    internal SendResult PublishNoWaitResult(string topic, Message message)
    {
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        EnsureNotDisposed();
        return PublishNoWaitSingleCore(GetValidatedPublishTopicUtf8(topic,
            nameof(topic)), message);
    }

    internal bool Publish(string topic, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        ValidateTopicId(topic, nameof(topic));
        EnsureNotDisposed();
        RequestReplySupport.EnsureParts(parts, nameof(parts));

        if ((flags & SendFlags.DontWait) != 0)
            return SocketKernel.TrySendOrThrow(PublishNoWaitResult(topic,
                parts));

        if (parts is Message[] array)
        {
            PublishPartsWithFlags(topic, array, (int)flags, nameof(parts));
            return true;
        }

        if (parts is List<Message> list)
        {
            PublishPartsWithFlags(topic, list, (int)flags, nameof(parts));
            return true;
        }

        var copied = new Message[parts.Count];
        for (var i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        PublishPartsWithFlags(topic, copied, (int)flags, nameof(parts));
        return true;
    }

    internal SendResult PublishNoWaitResult(string topic,
        IReadOnlyList<Message> parts)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        ValidateTopicId(topic, nameof(topic));
        EnsureNotDisposed();
        RequestReplySupport.EnsureParts(parts, nameof(parts));

        if (parts is Message[] array)
            return PublishNoWaitParts(topic, array, nameof(parts));

        if (parts is List<Message> list)
            return PublishNoWaitParts(topic, list, nameof(parts));

        var copied = new Message[parts.Count];
        for (var i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        return PublishNoWaitParts(topic, copied, nameof(parts));
    }

    internal bool SendToChannel(string channelName, Message message,
        SendFlags flags = SendFlags.None)
    {
        ValidateChannelName(channelName, nameof(channelName));
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        EnsureNotDisposed();
        try
        {
            SubmitSingleChannel(channelName, message, (int)flags,
                false);
            return true;
        }
        catch (ZlinkException error) when ((flags & SendFlags.DontWait) != 0
                                           && RequestReplySupport.MapSendNoWaitResult(error)
                                           == SendResult.Backpressured)
        {
            return false;
        }
    }

    internal bool SendToChannel(string channelName, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        ValidateChannelName(channelName, nameof(channelName));
        EnsureNotDisposed();
        RequestReplySupport.EnsureParts(parts, nameof(parts));

        try
        {
            if (parts is Message[] array)
            {
                SendToChannelCore(channelName, array, (int)flags,
                    nameof(parts));
                return true;
            }

            if (parts is List<Message> list)
            {
                SendToChannelCore(channelName, CollectionsMarshal.AsSpan(list),
                    (int)flags, nameof(parts));
                return true;
            }

            var copied = new Message[parts.Count];
            for (var i = 0; i < copied.Length; i++)
                copied[i] = parts[i];
            SendToChannelCore(channelName, copied.AsSpan(), (int)flags,
                nameof(parts));
            return true;
        }
        catch (ZlinkException error) when ((flags & SendFlags.DontWait) != 0
                                           && RequestReplySupport.MapSendNoWaitResult(error)
                                           == SendResult.Backpressured)
        {
            return false;
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
        var submitted = false;
        try
        {
            message.MoveTo(ref nativePart);
            fixed (byte* topicPtr = topicUtf8)
            {
                var rc = NativeMethods.zlink_spot_publish_part_utf8(Handle,
                    topicPtr, ref nativePart, flags,
                    NativeMethods.ZlinkPartFlag.Final);
                submitted = true;
                if (rc != 0)
                    throw ZlinkException.CreateSubmitException(
                        NativeMethods.zlink_errno());
            }
        }
        catch
        {
            if (!submitted)
                message.RestoreFrom(ref nativePart);
            throw;
        }
    }

    private static void ValidateTopicId(string value, string paramName)
    {
        BoundaryValidation.ValidateTopicOrFilterUtf8(value, paramName,
            false);
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

    private byte[] GetChannelNameUtf8(string channelName)
    {
        var cached = _channelNameCacheUtf8;
        var cachedKey = _channelNameCacheKey;
        if (cached != null
            && (ReferenceEquals(cachedKey, channelName)
                || string.Equals(cachedKey, channelName, StringComparison.Ordinal)))
            return cached;

        var encoded = PublishTopicEncoding.GetNullTerminatedUtf8(channelName);
        _channelNameCacheKey = channelName;
        _channelNameCacheUtf8 = encoded;
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

        ValidateTopicId(topic, paramName);
        return GetPublishTopicUtf8(topic);
    }

    private static void ValidateChannelName(string value, string paramName)
    {
        BoundaryValidation.ValidateFixedUtf8(value, paramName);
    }

    private void PublishPartsWithFlags(string topic, IReadOnlyList<Message> parts,
        int flags, string paramName)
    {
        if (parts is Message[] array)
        {
            PublishCore(topic, array.AsSpan(), flags, paramName);
            return;
        }

        if (parts is List<Message> list)
        {
            PublishCore(topic, CollectionsMarshal.AsSpan(list), flags,
                paramName);
            return;
        }

        var copied = new Message[parts.Count];
        for (var i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        PublishCore(topic, copied.AsSpan(), flags, paramName);
    }

    private SendResult PublishNoWaitParts(string topic,
        IReadOnlyList<Message> parts, string paramName)
    {
        if (parts is Message[] array)
            return PublishNoWaitCore(topic, array.AsSpan(), paramName);

        if (parts is List<Message> list)
            return PublishNoWaitCore(topic, CollectionsMarshal.AsSpan(list),
                paramName);

        var copied = new Message[parts.Count];
        for (var i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        return PublishNoWaitCore(topic, copied.AsSpan(), paramName);
    }
}