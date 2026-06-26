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
    IZLinkBoundSessionSendCall PacketName(string packetName);

    IZLinkBoundSessionSendCall Metadata(
        string key,
        string value);

    ValueTask Async(CancellationToken cancellationToken = default);

    ValueTask Yield(CancellationToken cancellationToken = default)
    {
        _ = cancellationToken;
        throw new NotSupportedException("Yield is not supported by this bound session send call.");
    }
}
