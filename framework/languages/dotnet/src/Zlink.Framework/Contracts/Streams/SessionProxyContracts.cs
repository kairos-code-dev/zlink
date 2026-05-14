namespace Zlink.Framework.Contracts.Streams;

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
    IZLinkSessionProxySendCall PacketName(string packetName);

    IZLinkSessionProxySendCall Metadata(
        string key,
        string value);

    ValueTask Submit(CancellationToken cancellationToken = default);
}

public interface IZLinkSessionProxyRequestCall
{
    IZLinkSessionProxyRequestCall PacketName(string packetName);

    IZLinkSessionProxyRequestCall Metadata(
        string key,
        string value);

    IZLinkSessionProxyRequestCall Timeout(TimeSpan timeout);

    ValueTask<TReply> SubmitAsync<TReply>(CancellationToken cancellationToken = default);
}
