// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

internal sealed class SpotNodePublisherOptions
{
    private readonly SpotNode _node;

    internal SpotNodePublisherOptions(SpotNode node)
    {
        _node = node;
    }

    public int SendHighWaterMark
    {
        set => _node.SetPubSubHighWaterMark(value);
    }

    public TimeSpan? SendTimeout
    {
        set => _node.SetOption(SpotNodeSocketRole.Pub, SocketOptions.SndTimeo,
            CommonSocketOptions.EncodeDuration(value, nameof(value)));
    }

    public TimeSpan? Linger
    {
        set => _node.SetOption(SpotNodeSocketRole.Pub, SocketOptions.Linger,
            CommonSocketOptions.EncodeDuration(value, nameof(value)));
    }

    public bool NoDrop
    {
        set => _node.SetOption(SpotNodeSocketRole.Pub, SocketOptions.XPubNoDrop,
            value ? 1 : 0);
    }
}

internal sealed class SpotNodeSubscriberOptions
{
    private readonly SpotNode _node;

    internal SpotNodeSubscriberOptions(SpotNode node)
    {
        _node = node;
    }

    public int ReceiveHighWaterMark
    {
        set => _node.SetPubSubHighWaterMark(value);
    }

    public TimeSpan? ReceiveTimeout
    {
        set => _node.SetOption(SpotNodeSocketRole.Sub, SocketOptions.RcvTimeo,
            CommonSocketOptions.EncodeDuration(value, nameof(value)));
    }

    public TimeSpan? Linger
    {
        set => _node.SetOption(SpotNodeSocketRole.Sub, SocketOptions.Linger,
            CommonSocketOptions.EncodeDuration(value, nameof(value)));
    }
}
