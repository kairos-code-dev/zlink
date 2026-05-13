namespace Zlink.Framework.Streams;

public interface IZLinkSessionProxy
{
    IZLinkSessionProxySendCall Send<TMessage>(
        string actorId,
        TMessage message);

    IZLinkSessionProxyRequestCall Request<TRequest>(
        string actorId,
        TRequest request);
}

public interface IZLinkSessionProxySendCall
{
    IZLinkSessionProxySendCall WithPacketName(string packetName);

    IZLinkSessionProxySendCall WithMetadata(
        string key,
        string value);

    ValueTask Submit(CancellationToken cancellationToken = default);
}

public interface IZLinkSessionProxyRequestCall
{
    IZLinkSessionProxyRequestCall WithPacketName(string packetName);

    IZLinkSessionProxyRequestCall WithMetadata(
        string key,
        string value);

    IZLinkSessionProxyRequestCall WithTimeout(TimeSpan timeout);

    ValueTask<TReply> SubmitAsync<TReply>(CancellationToken cancellationToken = default);
}
