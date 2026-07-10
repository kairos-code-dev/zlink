using Microsoft.Extensions.Logging;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotSubscriptionRegistry
{
    private readonly Dictionary<string, List<ZLinkSpotSubscriptionDescriptor>> _descriptorsByTopic =
        new(StringComparer.Ordinal);

    private readonly List<ZLinkSpotSubscriptionRegistration> _registrations = [];

    public void Add(string topic, Type handlerType)
    {
        if (string.IsNullOrWhiteSpace(topic))
            throw new ZLinkConfigurationException("SPOT subscription topic must not be empty.");

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

            if (!received) return;

            await DispatchMessageAsync(message, codecs, dispatchErrors, logger, dispatchAsync, cancellationToken)
                .ConfigureAwait(false);
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
        if (message.Parts.Count == 0)
        {
            CreateScope("<unknown>", message.Topic)
                .Dropped(
                    logger,
                    dispatchErrors,
                    LogLevel.Warning,
                    ZLinkDispatchErrorReason.InvalidFrame,
                    "invalid-frame");
            return;
        }

        if (!_descriptorsByTopic.TryGetValue(message.Topic, out var descriptors))
        {
            CreateScope("<unknown>", message.Topic)
                .Dropped(logger, dispatchErrors, LogLevel.Debug);
            return;
        }

        var header = ZLinkEnvelopeCodec.DecodeHeader(message.Parts);
        var scope = CreateScope(
            header.MessageName,
            message.Topic,
            header.ContentType,
            header.CorrelationId,
            header.Source);

        scope.Trace(dispatchErrors, ZLinkMessageFlowOutcome.Received);

        var dispatched = false;
        foreach (var descriptor in descriptors)
        {
            if (!string.Equals(descriptor.MessageName, header.MessageName, StringComparison.Ordinal)) continue;

            var body = ZLinkEnvelopeCodec.DecodeBody(message.Parts, descriptor.MessageType, codecs);
            await dispatchAsync(descriptor, body, cancellationToken).ConfigureAwait(false);
            dispatched = true;
        }

        if (dispatched)
            scope.Trace(dispatchErrors, ZLinkMessageFlowOutcome.Dispatched);

        if (!dispatched)
        {
            scope.Dropped(logger, dispatchErrors, LogLevel.Debug);
        }
    }

    private static ZLinkDispatchFlowScope CreateScope(
        string? packetName,
        string topic,
        string? contentType = null,
        string? correlationId = null,
        string? sourceRid = null)
    {
        return new ZLinkDispatchFlowScope(
            ZLinkDispatchErrorSurface.SpotSubscription,
            "SpotSubscription",
            ZLinkDispatchMessageKind.Publish,
            "Publish",
            packetName ?? "<unknown>",
            topic: topic,
            contentType: contentType,
            correlationId: correlationId,
            sourceRid: sourceRid);
    }
}
