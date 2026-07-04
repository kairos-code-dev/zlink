namespace Zlink.Framework.Contracts.Channels;

public interface IZLinkSendCall
{
    IZLinkSendCall PacketName(string messageName);

    void Submit(CancellationToken cancellationToken = default);
}

public interface IZLinkRequestCall
{
    IZLinkRequestCall PacketName(string messageName);

    IZLinkRequestCall Timeout(TimeSpan timeout);

    ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default);
}

public interface IZLinkYieldRequestCall : IZLinkRequestCall
{
    new IZLinkYieldRequestCall PacketName(string messageName);

    new IZLinkYieldRequestCall Timeout(TimeSpan timeout);

    ValueTask<TReply> Yield<TReply>(CancellationToken cancellationToken = default);
}

public interface IZLinkPublishCall
{
    IZLinkPublishCall PacketName(string messageName);

    void Submit(CancellationToken cancellationToken = default);
}
