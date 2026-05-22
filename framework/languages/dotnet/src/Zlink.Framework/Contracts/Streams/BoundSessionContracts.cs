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

    ValueTask Submit(CancellationToken cancellationToken = default);
}
