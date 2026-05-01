using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Backend.Contracts;

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

    public IZLinkRequestCall RequestChannel<TMessage>(
        string channelName,
        TMessage request)
    {
        return new ZLinkCurrentSpotRequestCall<TMessage>(
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
    private string? _messageName = ZLinkMessageNameResolver.ResolveFromMessage(message);

    public IZLinkSendCall WithPacketName(string messageName)
    {
        _messageName = messageName;
        return this;
    }

    public ValueTask Async(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Command,
            channelName,
            _messageName ?? throw new InvalidOperationException("Message name is required."),
            ZLinkEnvelopeCodec.DefaultContentType,
            Guid.NewGuid().ToString("N"),
            null,
            null,
            null,
            null);
        var envelope = ZLinkEnvelopeCodec.Encode(header, message, message?.GetType() ?? typeof(TMessage));
        return activation.SendChannelAsync(channelName, envelope, cancellationToken);
    }
}

internal sealed class ZLinkCurrentSpotRequestCall<TMessage>(
    ZLinkSpotActivation activation,
    string channelName,
    TMessage request) : IZLinkRequestCall
{
    private string? _messageName = ZLinkMessageNameResolver.ResolveFromMessage(request);
    private TimeSpan? _timeout;

    public IZLinkRequestCall WithPacketName(string messageName)
    {
        _messageName = messageName;
        return this;
    }

    public IZLinkRequestCall WithTimeout(TimeSpan timeout)
    {
        _timeout = timeout;
        return this;
    }

    public async ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default)
    {
        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Request,
            channelName,
            _messageName ?? throw new InvalidOperationException("Message name is required."),
            ZLinkEnvelopeCodec.DefaultContentType,
            Guid.NewGuid().ToString("N"),
            DateTimeOffset.UtcNow.Add(_timeout ?? TimeSpan.FromSeconds(30)),
            null,
            null,
            null);
        using var envelope = ZLinkEnvelopeCodec.Encode(header, request, request?.GetType() ?? typeof(TMessage));
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
