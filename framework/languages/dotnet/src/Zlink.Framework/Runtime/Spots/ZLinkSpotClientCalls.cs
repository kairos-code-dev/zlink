using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Backend;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotClientService : IZLinkSpotClient
{
    public IZLinkSendCall SendChannel<TMessage>(string channelName, TMessage message)
    {
        return new ZLinkCurrentSpotSendCall<TMessage>(
            ZLinkSpotAmbientContext.RequireCurrent(),
            channelName,
            message);
    }

    public IZLinkRequestCall<TReply> RequestChannel<TReply>(
        string channelName,
        IZLinkRequest<TReply> request)
    {
        return new ZLinkCurrentSpotRequestCall<TReply>(
            ZLinkSpotAmbientContext.RequireCurrent(),
            channelName,
            request);
    }

    public IZLinkPublishCall Publish<TEvent>(string topic, TEvent message)
    {
        return ZLinkSpotAmbientContext.RequireCurrent().Publish(topic, message);
    }
}

internal sealed class ZLinkCurrentSpotSendCall<TMessage>(
    ZLinkSpotActivation activation,
    string channelName,
    TMessage message) : IZLinkSendCall
{
    private string? _packetName = ZLinkPacketNameResolver.ResolveFromMessage(message);
    private global::Zlink.SendFlags _flags;

    public IZLinkSendCall WithPacketName(string packetName)
    {
        _packetName = packetName;
        return this;
    }

    public IZLinkSendCall WithDontWait()
    {
        _flags |= global::Zlink.SendFlags.DontWait;
        return this;
    }

    public bool Exec()
    {
        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Command,
            channelName,
            _packetName ?? throw new InvalidOperationException("Packet name is required."),
            ZLinkEnvelopeCodec.DefaultContentType,
            Guid.NewGuid().ToString("N"),
            null,
            null,
            null,
            null);
        using var envelope = ZLinkEnvelopeCodec.Encode(header, message, message?.GetType() ?? typeof(TMessage));
        return activation.SendChannel(channelName, envelope, _flags);
    }
}

internal sealed class ZLinkCurrentSpotRequestCall<TReply>(
    ZLinkSpotActivation activation,
    string channelName,
    IZLinkRequest<TReply> request) : IZLinkRequestCall<TReply>
{
    private string? _packetName = ZLinkPacketNameResolver.ResolveFromMessage(request);
    private TimeSpan? _timeout;

    public IZLinkRequestCall<TReply> WithPacketName(string packetName)
    {
        _packetName = packetName;
        return this;
    }

    public IZLinkRequestCall<TReply> WithTimeout(TimeSpan timeout)
    {
        _timeout = timeout;
        return this;
    }

    public async ValueTask<TReply> ExecAsync(CancellationToken cancellationToken = default)
    {
        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Request,
            channelName,
            _packetName ?? throw new InvalidOperationException("Packet name is required."),
            ZLinkEnvelopeCodec.DefaultContentType,
            Guid.NewGuid().ToString("N"),
            DateTimeOffset.UtcNow.Add(_timeout ?? TimeSpan.FromSeconds(30)),
            null,
            null,
            null);
        using var envelope = ZLinkEnvelopeCodec.Encode(header, request, request.GetType());
        var reply = await activation.RequestChannelAsync(channelName, envelope, _timeout, cancellationToken);
        try
        {
            if (reply.Count == 0)
            {
                throw new InvalidOperationException("SPOT channel request reply is empty.");
            }

            var replyHeader = ZLinkEnvelopeCodec.DecodeHeader(reply[0]);
            if (replyHeader.Kind == ZLinkMessageKind.Error)
            {
                throw new InvalidOperationException(replyHeader.ErrorMessage ?? "SPOT channel request failed.");
            }

            return (TReply?)ZLinkEnvelopeCodec.DecodeBody(reply[0], typeof(TReply))
                ?? throw new InvalidOperationException("SPOT channel reply body is null.");
        }
        finally
        {
            foreach (var item in reply)
            {
                item.Dispose();
            }
        }
    }
}
