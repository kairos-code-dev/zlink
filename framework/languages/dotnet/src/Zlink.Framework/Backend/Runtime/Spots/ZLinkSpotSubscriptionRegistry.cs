using Zlink.Framework.Backend.Contracts;

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

    public void Bind(IZLinkSpot spot, IZLinkBackendSpot nativeSpot)
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
        Func<ZLinkSpotSubscriptionDescriptor, object?, CancellationToken, ValueTask> dispatchAsync,
        CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            using var message = nativeSpot.Subscribe(RecvFlags.DontWait);
            if (message is null)
            {
                return;
            }

            await DispatchMessageAsync(message, dispatchAsync, cancellationToken).ConfigureAwait(false);
        }
    }

    private async ValueTask DispatchMessageAsync(
        TopicMessage message,
        Func<ZLinkSpotSubscriptionDescriptor, object?, CancellationToken, ValueTask> dispatchAsync,
        CancellationToken cancellationToken)
    {
        Interlocked.Increment(ref _messageCount);
        LastTopic = message.Topic;

        if (!_descriptorsByTopic.TryGetValue(message.Topic, out var descriptors)
            || message.Parts.Count == 0)
        {
            Interlocked.Increment(ref _ignoreCount);
            return;
        }

        var header = ZLinkEnvelopeCodec.DecodeHeader(message.Parts);
        LastMessageName = header.MessageName;
        var dispatched = false;
        foreach (var descriptor in descriptors)
        {
            if (!string.Equals(descriptor.MessageName, header.MessageName, StringComparison.Ordinal))
            {
                continue;
            }

            var body = ZLinkEnvelopeCodec.DecodeBody(message.Parts, descriptor.MessageType);
            await dispatchAsync(descriptor, body, cancellationToken).ConfigureAwait(false);
            dispatched = true;
            Interlocked.Increment(ref _dispatchCount);
        }

        if (!dispatched)
        {
            Interlocked.Increment(ref _ignoreCount);
        }
    }
}
