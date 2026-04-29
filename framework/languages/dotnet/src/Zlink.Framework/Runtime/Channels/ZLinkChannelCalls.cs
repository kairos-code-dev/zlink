using System.Collections.Concurrent;
using System.Reflection;
using System.Text.Json;
using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Backend.Contracts;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkSendCall : IZLinkSendCall
{
    private readonly ZLinkFrameworkRuntime _runtime;
    private readonly string _channelName;
    private readonly object? _message;
    private string? _messageName;
    private SendFlags _flags;

    public ZLinkSendCall(
        ZLinkFrameworkRuntime runtime,
        ZLinkFrameworkRegistration registration,
        string channelName,
        object? message)
    {
        _runtime = runtime;
        _channelName = channelName;
        _message = message;
        _messageName = ZLinkMessageNameResolver.ResolveFromMessage(message);
        _ = registration;
    }

    public IZLinkSendCall WithMessageName(string messageName)
    {
        _messageName = messageName;
        return this;
    }

    public IZLinkSendCall WithDontWait()
    {
        _flags |= SendFlags.DontWait;
        return this;
    }

    public bool Sync()
    {
        var bundle = _runtime.GetOrCreateClientBundle(_channelName);
        var dealer = (IZLinkBackendDealerSocket)bundle.Socket;
        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Command,
            _channelName,
            _messageName ?? throw new InvalidOperationException("Message name is required."),
            ZLinkEnvelopeCodec.DefaultContentType,
            Guid.NewGuid().ToString("N"),
            null,
            null,
            null,
            null);

        using var message = ZLinkEnvelopeCodec.Encode(header, _message, _message?.GetType());
        return dealer.Send(message, _flags);
    }
}

internal sealed class ZLinkRequestCall<TMessage> : IZLinkRequestCall
{
    private readonly ZLinkFrameworkRuntime _runtime;
    private readonly ZLinkFrameworkRegistration _registration;
    private readonly string _channelName;
    private readonly TMessage _request;
    private string? _messageName;
    private TimeSpan? _timeout;

    public ZLinkRequestCall(
        ZLinkFrameworkRuntime runtime,
        ZLinkFrameworkRegistration registration,
        string channelName,
        TMessage request)
    {
        _runtime = runtime;
        _registration = registration;
        _channelName = channelName;
        _request = request;
        _messageName = ZLinkMessageNameResolver.ResolveFromMessage(request);
    }

    public IZLinkRequestCall WithMessageName(string messageName)
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
        var bundle = _runtime.GetOrCreateClientBundle(_channelName);
        var dealer = (IZLinkBackendDealerSocket)bundle.Socket;
        var correlationId = Guid.NewGuid().ToString("N");
        var timeout = _timeout ?? _registration.DefaultTimeout;
        var deadline = DateTimeOffset.UtcNow.Add(timeout);
        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Request,
            _channelName,
            _messageName ?? throw new InvalidOperationException("Message name is required."),
            ZLinkEnvelopeCodec.DefaultContentType,
            correlationId,
            deadline,
            null,
            null,
            null);
        using var message = ZLinkEnvelopeCodec.Encode(header, _request, _request?.GetType() ?? typeof(TMessage));
        var reply = await dealer.RequestAsync(message, timeout, cancellationToken).ConfigureAwait(false);
        try
        {
            if (reply.Count == 0)
            {
                throw new InvalidOperationException("ZLink request reply is empty.");
            }

            var replyHeader = ZLinkEnvelopeCodec.DecodeHeader(reply[0]);
            if (replyHeader.Kind == ZLinkMessageKind.Error)
            {
                throw new InvalidOperationException(replyHeader.ErrorMessage ?? "ZLink request failed.");
            }

            return (TReply?)ZLinkEnvelopeCodec.DecodeBody(reply[0], typeof(TReply))
                ?? throw new InvalidOperationException("ZLink request reply body is null.");
        }
        finally
        {
            foreach (var replyPart in reply)
            {
                replyPart.Dispose();
            }
        }
    }
}

internal sealed class ZLinkPublishCall : IZLinkPublishCall
{
    private readonly ZLinkFrameworkRuntime _runtime;
    private readonly string _channelName;
    private readonly string _topic;
    private readonly object? _message;
    private string? _messageName;
    private SendFlags _flags;

    public ZLinkPublishCall(
        ZLinkFrameworkRuntime runtime,
        string channelName,
        string topic,
        object? message)
    {
        _runtime = runtime;
        _channelName = channelName;
        _topic = topic;
        _message = message;
        _messageName = ZLinkMessageNameResolver.ResolveFromMessage(message);
    }

    public IZLinkPublishCall WithMessageName(string messageName)
    {
        _messageName = messageName;
        return this;
    }

    public IZLinkPublishCall WithDontWait()
    {
        _flags |= SendFlags.DontWait;
        return this;
    }

    public bool Sync()
    {
        var bundle = _runtime.GetOrCreatePublisherBundle(_channelName);
        var publisher = (IZLinkBackendPublisherSocket)bundle.Socket;
        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Event,
            _channelName,
            _messageName ?? throw new InvalidOperationException("Message name is required."),
            ZLinkEnvelopeCodec.DefaultContentType,
            Guid.NewGuid().ToString("N"),
            null,
            _topic,
            null,
            null);
        using var message = ZLinkEnvelopeCodec.Encode(header, _message, _message?.GetType());
        return publisher.Publish(_topic, message, _flags);
    }
}
