// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
///     A received publish: its topic, source routing id, and message parts. Owns
///     its parts until disposed.
/// </summary>
public sealed partial class TopicMessage : IDisposable
{
    /// <summary>
    ///     Creates a topic message instance.
    /// </summary>
    public TopicMessage()
    {
    }

    /// <summary>
    ///     Gets the routing id.
    /// </summary>
    public RoutingId? RoutingId
    {
        get
        {
            if (_routingIdSnapshot.HasValue)
                _routingId ??= _routingIdSnapshot.ToRoutingId();
            return _routingId;
        }
    }

    /// <summary>
    ///     Decodes topic bytes to a topic string.
    /// </summary>
    public string Topic => _topic ??= DecodeTopicBytes();

    /// <summary>
    ///     Gets the parts.
    /// </summary>
    public IReadOnlyList<Message> Parts => PartsCollection;

    /// <summary>
    ///     Gets whether this publish carries exactly one part.
    /// </summary>
    public bool IsSinglePart => _singlePart != null || PartsCollection.IsSinglePart;

    /// <summary>
    ///     Releases resources owned by this instance.
    /// </summary>
    public void Dispose()
    {
        if (Interlocked.Exchange(ref _closed, 1) != 0)
            return;
        if (_parts != null)
            _parts.Dispose();
        else
            _singlePart?.Dispose();
        _parts = null;
        _singlePart = null;
        _routingId = null;
        _routingIdSnapshot = default;
        _topicBytes = null;
        _topicWriteBuffer = null;
        _topicLength = 0;
        _topic = string.Empty;
    }

    /// <summary>
    ///     Returns the first message part; it stays owned by this envelope.
    /// </summary>
    public Message FirstPart()
    {
        return _singlePart ?? PartsCollection.First();
    }

    /// <summary>
    ///     Returns the only message part, or throws when the publish is multipart;
    ///     the part stays owned by this envelope.
    /// </summary>
    public Message SinglePartOrThrow()
    {
        return _singlePart ?? PartsCollection.Single();
    }
}