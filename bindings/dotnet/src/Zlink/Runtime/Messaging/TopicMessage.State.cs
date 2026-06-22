// SPDX-License-Identifier: MPL-2.0

using System;
using System.Text;

namespace Systems.Zlink;

public sealed partial class TopicMessage
{
    private MultipartMessageCollection? _parts;
    private Message? _singlePart;
    private int _closed;
    private RoutingId? _routingId;
    private RoutingIdSnapshot _routingIdSnapshot;
    private string? _topic = string.Empty;
    private byte[]? _topicBytes;
    private byte[]? _topicWriteBuffer;
    private int _topicLength;

    internal TopicMessage(RoutingId? routingId, string topic, Message[] parts)
        : this(routingId, topic,
            MultipartMessageCollection.FromMessages(parts))
    {
    }

    internal TopicMessage(RoutingId? routingId, string topic,
        MultipartMessageCollection parts)
    {
        Populate(routingId, topic, parts);
    }

    internal TopicMessage(RoutingId? routingId, string topic,
        Message singlePart)
    {
        PopulateSinglePart(routingId, topic, singlePart);
    }

    internal TopicMessage(RoutingIdSnapshot routingId, string topic,
        MultipartMessageCollection parts)
    {
        Populate(routingId, topic, parts);
    }

    internal TopicMessage(RoutingIdSnapshot routingId, string topic,
        Message singlePart)
    {
        PopulateSinglePart(routingId, topic, singlePart);
    }

    internal void Populate(RoutingId? routingId, string topic,
        MultipartMessageCollection parts)
    {
        ResetForReuse();
        _routingId = routingId;
        _routingIdSnapshot = default;
        SetTopic(topic);
        _parts = parts ?? MultipartMessageCollection.FromMessages(Array.Empty<Message>());
    }

    internal void PopulateSinglePart(RoutingId? routingId, string topic,
        Message singlePart)
    {
        if (singlePart == null)
            throw new ArgumentNullException(nameof(singlePart));
        ResetForReuse();
        _routingId = routingId;
        _routingIdSnapshot = default;
        SetTopic(topic);
        _singlePart = singlePart;
    }

    internal void Populate(RoutingIdSnapshot routingId, string topic,
        MultipartMessageCollection parts)
    {
        ResetForReuse();
        _routingIdSnapshot = routingId;
        SetTopic(topic);
        _parts = parts ?? MultipartMessageCollection.FromMessages(Array.Empty<Message>());
    }

    internal void PopulateSinglePart(RoutingIdSnapshot routingId, string topic,
        Message singlePart)
    {
        if (singlePart == null)
            throw new ArgumentNullException(nameof(singlePart));
        ResetForReuse();
        _routingIdSnapshot = routingId;
        SetTopic(topic);
        _singlePart = singlePart;
    }

    internal void Populate(RoutingId? routingId, byte[] topicBuffer,
        int topicLength, MultipartMessageCollection parts)
    {
        ResetForReuse();
        _routingId = routingId;
        _routingIdSnapshot = default;
        CopyTopic(topicBuffer, topicLength);
        _parts = parts ?? MultipartMessageCollection.FromMessages(Array.Empty<Message>());
    }

    internal void PopulateSinglePart(RoutingId? routingId, byte[] topicBuffer,
        int topicLength, Message singlePart)
    {
        if (singlePart == null)
            throw new ArgumentNullException(nameof(singlePart));
        ResetForReuse();
        _routingId = routingId;
        _routingIdSnapshot = default;
        CopyTopic(topicBuffer, topicLength);
        _singlePart = singlePart;
    }

    internal byte[] GetWritableTopicBuffer(int minimumLength)
    {
        if (minimumLength < 0)
            throw new ArgumentOutOfRangeException(nameof(minimumLength));
        byte[]? topicWriteBuffer = _topicWriteBuffer;
        if (topicWriteBuffer == null || topicWriteBuffer.Length < minimumLength)
        {
            topicWriteBuffer = new byte[minimumLength];
            _topicWriteBuffer = topicWriteBuffer;
        }
        return topicWriteBuffer;
    }

    internal void PopulateFromWritableTopicBuffer(RoutingId? routingId,
        int topicLength, MultipartMessageCollection parts)
    {
        ResetForReuse();
        _routingId = routingId;
        _routingIdSnapshot = default;
        SetTopicFromWritableBuffer(topicLength);
        _parts = parts ?? MultipartMessageCollection.FromMessages(Array.Empty<Message>());
    }

    internal void PopulateSinglePartFromWritableTopicBuffer(
        RoutingId? routingId, int topicLength, Message singlePart)
    {
        if (singlePart == null)
            throw new ArgumentNullException(nameof(singlePart));
        ResetForReuse();
        _routingId = routingId;
        _routingIdSnapshot = default;
        SetTopicFromWritableBuffer(topicLength);
        _singlePart = singlePart;
    }

    internal void PopulateFromWritableTopicBuffer(
        RoutingIdSnapshot routingId, int topicLength,
        MultipartMessageCollection parts)
    {
        ResetForReuse();
        _routingIdSnapshot = routingId;
        SetTopicFromWritableBuffer(topicLength);
        _parts = parts ?? MultipartMessageCollection.FromMessages(Array.Empty<Message>());
    }

    internal void PopulateSinglePartFromWritableTopicBuffer(
        RoutingIdSnapshot routingId, int topicLength, Message singlePart)
    {
        if (singlePart == null)
            throw new ArgumentNullException(nameof(singlePart));
        ResetForReuse();
        _routingIdSnapshot = routingId;
        SetTopicFromWritableBuffer(topicLength);
        _singlePart = singlePart;
    }

    private void ResetForReuse()
    {
        if (_parts != null)
            _parts.Dispose();
        else
            _singlePart?.Dispose();
        _parts = null;
        _singlePart = null;
        _routingId = null;
        _routingIdSnapshot = default;
        _topic = string.Empty;
        _topicLength = 0;
        _closed = 0;
    }

    private void SetTopic(string? topic)
    {
        _topic = topic ?? string.Empty;
        _topicLength = 0;
    }

    private void CopyTopic(byte[] topicBuffer, int topicLength)
    {
        if (topicLength <= 0)
        {
            _topic = string.Empty;
            _topicLength = 0;
            return;
        }

        if ((uint)topicLength > (uint)topicBuffer.Length)
            throw new ArgumentOutOfRangeException(nameof(topicLength));

        byte[] topicBytes = _topicBytes != null
            && _topicBytes.Length >= topicLength
                ? _topicBytes
                : new byte[topicLength];
        Buffer.BlockCopy(topicBuffer, 0, topicBytes, 0, topicLength);
        _topicBytes = topicBytes;
        _topicLength = topicLength;
        _topic = null;
    }

    private void SetTopicFromWritableBuffer(int topicLength)
    {
        if (topicLength <= 0)
        {
            _topic = string.Empty;
            _topicLength = 0;
            return;
        }

        byte[]? topicWriteBuffer = _topicWriteBuffer;
        if (topicWriteBuffer == null
            || (uint)topicLength > (uint)topicWriteBuffer.Length)
        {
            throw new ArgumentOutOfRangeException(nameof(topicLength));
        }

        // HOT PATH: Spot.Subscribe(TopicMessage, ...) writes topic bytes into
        // the reusable buffer supplied by this instance. Swap buffers instead
        // of copying, and keep UTF-8 decoding lazy so callers that only inspect
        // the payload do not pay for a string allocation.
        _topicWriteBuffer = _topicBytes;
        _topicBytes = topicWriteBuffer;
        _topicLength = topicLength;
        _topic = null;
    }

    private string DecodeTopicBytes()
    {
        return _topicBytes == null || _topicLength == 0
            ? string.Empty
            : Encoding.UTF8.GetString(_topicBytes, 0, _topicLength);
    }

    private MultipartMessageCollection PartsCollection
    {
        get
        {
            if (_parts != null)
                return _parts;
            if (_singlePart == null)
                return _parts = MultipartMessageCollection.FromMessages(
                    Array.Empty<Message>());
            Message part = _singlePart;
            _singlePart = null;
            return _parts = MultipartMessageCollection.FromSingle(part);
        }
    }
}
