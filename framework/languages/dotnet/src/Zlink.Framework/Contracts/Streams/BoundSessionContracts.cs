namespace Zlink.Framework.Contracts.Streams;

public interface IZLinkBoundSession
{
    IZLinkBoundSessionSendCall Send<TMessage>(
        TMessage message);

    ValueTask DisconnectAsync(
        CancellationToken cancellationToken = default);
}

public interface IZLinkBoundSessionSendCall
{
    IZLinkBoundSessionSendCall Metadata(
        string key,
        string value);

    void Submit(CancellationToken cancellationToken = default);
}
