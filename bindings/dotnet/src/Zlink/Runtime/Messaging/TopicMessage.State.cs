// SPDX-License-Identifier: MPL-2.0

using System.Text;

namespace Systems.Zlink;

public sealed partial class TopicMessage
{
    private int _closed;
    private MultipartMessageCollection? _parts;
    private RoutingId? _routingId;
    private RoutingIdSnapshot _routingIdSnapshot;
    private Message? _singlePart;
    private string? _topic = string.Empty;
    private byte[]? _topicBytes;
    private int _topicLength;
    private byte[]? _topicWriteBuffer;

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

    private MultipartMessageCollection PartsCollection
    {
        get
        {
            if (_parts != null)
                return _parts;
            if (_singlePart == null)
                return _parts = MultipartMessageCollection.FromMessages(
                    Array.Empty<Message>());
            var part = _singlePart;
            _singlePart = null;
            return _parts = MultipartMessageCollection.FromSingle(part);
        }
    }

    internal void Populate(RoutingId? routingId, string topic,
        MultipartMessageCollection parts)
    {
        ResetForReuse();
        SetTopic(topic);
        PopulatePartsCore(routingId, default, parts);
    }

    internal void PopulateSinglePart(RoutingId? routingId, string topic,
        Message singlePart)
    {
        if (singlePart == null)
            throw new ArgumentNullException(nameof(singlePart));
        ResetForReuse();
        SetTopic(topic);
        PopulateSinglePartCore(routingId, default, singlePart);
    }

    internal void Populate(RoutingIdSnapshot routingId, string topic,
        MultipartMessageCollection parts)
    {
        ResetForReuse();
        SetTopic(topic);
        PopulatePartsCore(null, routingId, parts);
    }

    internal void PopulateSinglePart(RoutingIdSnapshot routingId, string topic,
        Message singlePart)
    {
        if (singlePart == null)
            throw new ArgumentNullException(nameof(singlePart));
        ResetForReuse();
        SetTopic(topic);
        PopulateSinglePartCore(null, routingId, singlePart);
    }

    internal void Populate(RoutingId? routingId, byte[] topicBuffer,
        int topicLength, MultipartMessageCollection parts)
    {
        ResetForReuse();
        CopyTopic(topicBuffer, topicLength);
        PopulatePartsCore(routingId, default, parts);
    }

    internal void PopulateSinglePart(RoutingId? routingId, byte[] topicBuffer,
        int topicLength, Message singlePart)
    {
        if (singlePart == null)
            throw new ArgumentNullException(nameof(singlePart));
        ResetForReuse();
        CopyTopic(topicBuffer, topicLength);
        PopulateSinglePartCore(routingId, default, singlePart);
    }

    internal byte[] GetWritableTopicBuffer(int minimumLength)
    {
        if (minimumLength < 0)
            throw new ArgumentOutOfRangeException(nameof(minimumLength));
        var topicWriteBuffer = _topicWriteBuffer;
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
        ResetForReuse(false);
        SetTopicFromWritableBuffer(topicLength);
        PopulatePartsCore(routingId, default, parts);
    }

    internal void PopulateSinglePartFromWritableTopicBuffer(
        RoutingId? routingId, int topicLength, Message singlePart)
    {
        if (singlePart == null)
            throw new ArgumentNullException(nameof(singlePart));
        ResetForReuse(false);
        SetTopicFromWritableBuffer(topicLength);
        PopulateSinglePartCore(routingId, default, singlePart);
    }

    internal void PopulateFromWritableTopicBuffer(
        RoutingIdSnapshot routingId, int topicLength,
        MultipartMessageCollection parts)
    {
        ResetForReuse(false);
        SetTopicFromWritableBuffer(topicLength);
        PopulatePartsCore(null, routingId, parts);
    }

    internal void PopulateSinglePartFromWritableTopicBuffer(
        RoutingIdSnapshot routingId, int topicLength, Message singlePart)
    {
        if (singlePart == null)
            throw new ArgumentNullException(nameof(singlePart));
        ResetForReuse(false);
        SetTopicFromWritableBuffer(topicLength);
        PopulateSinglePartCore(null, routingId, singlePart);
    }

    private void PopulatePartsCore(RoutingId? routingId,
        RoutingIdSnapshot routingIdSnapshot, MultipartMessageCollection? parts)
    {
        _routingId = routingId;
        _routingIdSnapshot = routingIdSnapshot;
        _parts = parts ?? MultipartMessageCollection.FromMessages(Array.Empty<Message>());
    }

    private void PopulateSinglePartCore(RoutingId? routingId,
        RoutingIdSnapshot routingIdSnapshot, Message singlePart)
    {
        _routingId = routingId;
        _routingIdSnapshot = routingIdSnapshot;
        _singlePart = singlePart;
    }

    private void ResetForReuse(bool resetTopic = true, bool reopen = true)
    {
        if (_parts != null)
            _parts.Dispose();
        else
            _singlePart?.Dispose();
        _parts = null;
        _singlePart = null;
        _routingId = null;
        _routingIdSnapshot = default;
        if (resetTopic)
        {
            _topic = string.Empty;
            _topicLength = 0;
        }

        if (reopen)
            _closed = 0;
    }

    private void DisposeCore()
    {
        if (Interlocked.Exchange(ref _closed, 1) != 0)
            return;

        ResetForReuse(reopen: false);
        _topicBytes = null;
        _topicWriteBuffer = null;
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

        var topicBytes = _topicBytes != null
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

        var topicWriteBuffer = _topicWriteBuffer;
        if (topicWriteBuffer == null
            || (uint)topicLength > (uint)topicWriteBuffer.Length)
            throw new ArgumentOutOfRangeException(nameof(topicLength));

        // HOT PATH: Spot.Subscribe(TopicMessage, ...) writes topic bytes into
        // the reusable buffer supplied by this instance. The reset step leaves
        // topic state for this method to replace, avoiding a transient empty
        // topic assignment before every subscribed message. Swap buffers instead
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
}
