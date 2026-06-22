using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Diagnostics;
using Microsoft.Extensions.Logging;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotSubscriptionRegistry
{
    private readonly List<ZLinkSpotSubscriptionRegistration> _registrations = [];
    private readonly Dictionary<string, List<ZLinkSpotSubscriptionDescriptor>> _descriptorsByTopic =
        new(StringComparer.Ordinal);
    private int _messageCount;
    private int _dispatchCount;
    private int _ignoreCount;

    public int MessageCount => Volatile.Read(ref _messageCount);

    public int DispatchCount => Volatile.Read(ref _dispatchCount);

    public int IgnoreCount => Volatile.Read(ref _ignoreCount);

    public string? LastTopic { get; private set; }

    public string? LastMessageName { get; private set; }

    public bool HasSubscriptions => _descriptorsByTopic.Count > 0;

    public void Add(string topic, Type handlerType)
    {
        if (string.IsNullOrWhiteSpace(topic))
        {
            throw new ZLinkConfigurationException("SPOT subscription topic must not be empty.");
        }

        _registrations.Add(new ZLinkSpotSubscriptionRegistration(topic, handlerType));
    }

    public void Bind(object spot, IZLinkBackendSpot nativeSpot)
    {
        foreach (var subscription in _registrations)
        {
            var descriptor = ZLinkSpotDescriptorFactory.CreateSubscriptionDescriptor(
                subscription.Topic,
                subscription.HandlerType,
                spot.GetType());

            if (!_descriptorsByTopic.TryGetValue(subscription.Topic, out var handlers))
            {
                handlers = [];
                _descriptorsByTopic.Add(subscription.Topic, handlers);
            }

            handlers.Add(descriptor);
            nativeSpot.SetSubscription(subscription.Topic);
        }
    }

    public async ValueTask DrainAsync(
        IZLinkBackendSpot nativeSpot,
        ZLinkCodecRegistryBuilder? codecs,
        ZLinkDispatchErrorReporter dispatchErrors,
        ILogger logger,
        Func<ZLinkSpotSubscriptionDescriptor, object?, CancellationToken, ValueTask> dispatchAsync,
        CancellationToken cancellationToken)
    {
        using var message = new TopicMessage();
        while (!cancellationToken.IsCancellationRequested)
        {
            bool received;
            try
            {
                received = nativeSpot.Subscribe(message, RecvFlags.DontWait);
            }
            catch (ZlinkRecvException ex)
                when (ex.Result is ZlinkRecvException.ErrorCode.NoData
                    or ZlinkRecvException.ErrorCode.Busy)
            {
                return;
            }

            if (!received)
            {
                return;
            }

            await DispatchMessageAsync(message, codecs, dispatchErrors, logger, dispatchAsync, cancellationToken).ConfigureAwait(false);
        }
    }

    private async ValueTask DispatchMessageAsync(
        TopicMessage message,
        ZLinkCodecRegistryBuilder? codecs,
        ZLinkDispatchErrorReporter dispatchErrors,
        ILogger logger,
        Func<ZLinkSpotSubscriptionDescriptor, object?, CancellationToken, ValueTask> dispatchAsync,
        CancellationToken cancellationToken)
    {
        Interlocked.Increment(ref _messageCount);
        LastTopic = message.Topic;

        if (message.Parts.Count == 0)
        {
            Interlocked.Increment(ref _ignoreCount);
            ZLinkMessageFlowLogger.Dropped(
                logger,
                LogLevel.Warning,
                "SpotSubscription",
                "Publish",
                "<unknown>",
                "invalid-frame",
                channelName: message.Topic);
            dispatchErrors.Report(new ZLinkMessageDispatchErrorEvent(
                ZLinkDispatchErrorSurface.SpotSubscription,
                ZLinkDispatchMessageKind.Publish,
                ZLinkDispatchErrorReason.InvalidFrame,
                ZLinkDispatchErrorAction.Drop,
                null,
                Topic: message.Topic));
            return;
        }

        if (!_descriptorsByTopic.TryGetValue(message.Topic, out var descriptors))
        {
            Interlocked.Increment(ref _ignoreCount);
            ZLinkMessageFlowLogger.Dropped(
                logger,
                LogLevel.Debug,
                "SpotSubscription",
                "Publish",
                "<unknown>",
                "no-handler",
                channelName: message.Topic);
            dispatchErrors.Report(new ZLinkMessageDispatchErrorEvent(
                ZLinkDispatchErrorSurface.SpotSubscription,
                ZLinkDispatchMessageKind.Publish,
                ZLinkDispatchErrorReason.HandlerMissing,
                ZLinkDispatchErrorAction.Drop,
                null,
                Topic: message.Topic));
            return;
        }

        var header = ZLinkEnvelopeCodec.DecodeHeader(message.Parts);
        LastMessageName = header.MessageName;

        if (dispatchErrors.Flow.Enabled(ZLinkMessageFlowPhase.Received))
        {
            dispatchErrors.Flow.Trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowPhase.Received,
                ZLinkDispatchErrorSurface.SpotSubscription,
                ZLinkDispatchMessageKind.Publish,
                PacketName: header.MessageName,
                Topic: message.Topic,
                SourceRid: header.Source,
                CorrelationId: header.CorrelationId));
        }

        var dispatched = false;
        foreach (var descriptor in descriptors)
        {
            if (!string.Equals(descriptor.MessageName, header.MessageName, StringComparison.Ordinal))
            {
                continue;
            }

            var body = ZLinkEnvelopeCodec.DecodeBody(message.Parts, descriptor.MessageType, codecs);
            await dispatchAsync(descriptor, body, cancellationToken).ConfigureAwait(false);
            dispatched = true;
            Interlocked.Increment(ref _dispatchCount);
        }

        if (dispatched && dispatchErrors.Flow.Enabled(ZLinkMessageFlowPhase.Dispatched))
        {
            dispatchErrors.Flow.Trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowPhase.Dispatched,
                ZLinkDispatchErrorSurface.SpotSubscription,
                ZLinkDispatchMessageKind.Publish,
                PacketName: header.MessageName,
                Topic: message.Topic,
                SourceRid: header.Source,
                CorrelationId: header.CorrelationId));
        }

        if (!dispatched)
        {
            Interlocked.Increment(ref _ignoreCount);
            ZLinkMessageFlowLogger.Dropped(
                logger,
                LogLevel.Debug,
                "SpotSubscription",
                "Publish",
                header.MessageName,
                "no-handler",
                channelName: message.Topic);
            dispatchErrors.Report(new ZLinkMessageDispatchErrorEvent(
                ZLinkDispatchErrorSurface.SpotSubscription,
                ZLinkDispatchMessageKind.Publish,
                ZLinkDispatchErrorReason.HandlerMissing,
                ZLinkDispatchErrorAction.Drop,
                header.MessageName,
                Topic: message.Topic,
                SourceRid: header.Source,
                CorrelationId: header.CorrelationId));
        }
    }
}
