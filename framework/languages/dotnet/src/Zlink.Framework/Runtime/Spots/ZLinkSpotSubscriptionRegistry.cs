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

    public void Add(string topic, Type spotType, System.Reflection.MethodInfo method)
    {
        if (string.IsNullOrWhiteSpace(topic))
            throw new ZLinkConfigurationException("SPOT subscription topic must not be empty.");
        _registrations.Add(new ZLinkSpotSubscriptionRegistration(topic, spotType, method));
    }

    public void Bind(object spot, IZLinkBackendSpot nativeSpot)
    {
        foreach (var (topic, handlers) in BuildDescriptors(spot))
        {
            _descriptorsByTopic.Add(topic, handlers);
            nativeSpot.SetSubscription(topic);
        }
    }

    public void Validate(object spot)
    {
        _ = BuildDescriptors(spot);
    }

    private Dictionary<string, List<ZLinkSpotSubscriptionDescriptor>> BuildDescriptors(object spot)
    {
        var descriptorsByTopic = new Dictionary<string, List<ZLinkSpotSubscriptionDescriptor>>(
            StringComparer.Ordinal);
        foreach (var subscription in _registrations)
        {
            var descriptor = subscription.Method is { } method
                ? ZLinkSpotDescriptorFactory.CreateAttributedSubscriptionDescriptor(
                    subscription.Topic, subscription.HandlerType, method)
                : ZLinkSpotDescriptorFactory.CreateSubscriptionDescriptor(
                    subscription.Topic, subscription.HandlerType, spot.GetType());

            if (!descriptorsByTopic.TryGetValue(subscription.Topic, out var handlers))
            {
                handlers = [];
                descriptorsByTopic.Add(subscription.Topic, handlers);
            }

            if (handlers.Any(existing => string.Equals(
                    existing.MessageName,
                    descriptor.MessageName,
                    StringComparison.Ordinal)))
                throw new ZLinkConfigurationException(
                    $"SPOT subscription handler for topic '{subscription.Topic}' and packet '{descriptor.MessageName}' is already registered.");
            handlers.Add(descriptor);
        }

        return descriptorsByTopic;
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
            using var invalidFlow = ZLinkFlowContext.Enter(
                null,
                null,
                dispatchErrors.Flow.CaptureEnabled,
                ZLinkFlowOrigin.Inbound);
            CreateScope("<unknown>", message.Topic)
                .Dropped(
                    logger,
                    dispatchErrors,
                    LogLevel.Warning,
                    ZLinkDispatchErrorReason.InvalidFrame,
                    "invalid-frame");
            return;
        }

        ZLinkEnvelopeHeader header;
        try
        {
            header = ZLinkEnvelopeCodec.DecodeHeader(message.Parts);
            ZLinkEnvelopeCodec.ValidateDispatchHeader(header);
        }
        catch (ZLinkEnvelopeProtocolException protocolError)
        {
            var validFlow = ZLinkEnvelopeCodec.ValidFlow(protocolError.Header);
            using var invalidFlow = ZLinkFlowContext.Enter(
                validFlow.FlowId,
                validFlow.FlowOrigin,
                dispatchErrors.Flow.CaptureEnabled,
                ZLinkFlowOrigin.Inbound);
            CreateScope(
                    protocolError.Header.MessageName,
                    message.Topic,
                    protocolError.Header.ContentType,
                    protocolError.Header.CorrelationId,
                    protocolError.Header.Source)
                .Dropped(
                    logger,
                    dispatchErrors,
                    LogLevel.Warning,
                    ZLinkDispatchErrorReason.InvalidFrame,
                    "invalid-frame");
            return;
        }
        using var currentFlow = ZLinkFlowContext.Enter(
            header.FlowId,
            header.FlowOrigin,
            dispatchErrors.Flow.CaptureEnabled,
            ZLinkFlowOrigin.Inbound);

        _descriptorsByTopic.TryGetValue(message.Topic, out var descriptors);
        ZLinkRuntimeMetrics.RecordFanoutReceived(descriptors is null ? null : message.Topic);
        var scope = CreateScope(
            header.MessageName,
            message.Topic,
            header.ContentType,
            header.CorrelationId,
            header.Source);

        scope.Trace(dispatchErrors, ZLinkMessageFlowOutcome.Received);

        if (descriptors is null)
        {
            scope.Dropped(logger, dispatchErrors, LogLevel.Debug);
            return;
        }

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
