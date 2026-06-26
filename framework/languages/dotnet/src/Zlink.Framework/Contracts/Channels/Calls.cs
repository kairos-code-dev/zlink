namespace Zlink.Framework.Contracts.Channels;

public interface IZLinkSendCall
{
    IZLinkSendCall PacketName(string messageName);

    ValueTask Async(CancellationToken cancellationToken = default);
}

public interface IZLinkRequestCall
{
    IZLinkRequestCall PacketName(string messageName);

    IZLinkRequestCall Timeout(TimeSpan timeout);

    ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default);

    ValueTask<TReply> Yield<TReply>(CancellationToken cancellationToken = default)
    {
        _ = cancellationToken;
        throw new NotSupportedException("Yield is not supported by this request call.");
    }
}

public interface IZLinkRouteRequestCall
{
    IZLinkRouteRequestCall PacketName(string messageName);

    IZLinkRouteRequestCall Timeout(TimeSpan timeout);

    ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default);
}

public interface IZLinkPublishCall
{
    IZLinkPublishCall PacketName(string messageName);

    ValueTask Async(CancellationToken cancellationToken = default);
}
