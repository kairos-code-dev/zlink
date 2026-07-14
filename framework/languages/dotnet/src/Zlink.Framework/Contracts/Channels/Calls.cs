namespace Zlink.Framework.Contracts.Channels;

public interface IZLinkSendCall
{
    void Submit(CancellationToken cancellationToken = default);
}

public interface IZLinkRequestCall
{
    IZLinkRequestCall Timeout(TimeSpan timeout);

    void Submit<TReply>(CancellationToken cancellationToken = default);

    ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default);

    ValueTask<TReply> Yield<TReply>(CancellationToken cancellationToken = default);
}

public interface IZLinkPublishCall
{
    void Submit(CancellationToken cancellationToken = default);
}
