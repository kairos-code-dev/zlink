namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotActorJoinRegistry
{
    private readonly List<ZLinkSpotActorJoinRegistration> _registrations = [];
    private readonly Dictionary<Type, ZLinkSpotActorJoinDescriptor> _descriptorsByRequestType = [];

    public bool HasHandlers => _descriptorsByRequestType.Count > 0;

    public void Add(
        Type handlerType,
        Type actorType,
        Type requestType,
        Type replyType)
    {
        _registrations.Add(new ZLinkSpotActorJoinRegistration(
            handlerType,
            actorType,
            requestType,
            replyType));
    }

    public void Bind(IZLinkSpot spot)
    {
        foreach (var actorJoin in _registrations)
        {
            var descriptor = ZLinkSpotDescriptorFactory.CreateActorJoinDescriptor(
                actorJoin.HandlerType,
                spot.GetType(),
                actorJoin.ActorType,
                actorJoin.RequestType,
                actorJoin.ReplyType);

            if (!_descriptorsByRequestType.TryAdd(descriptor.RequestType, descriptor))
            {
                throw new InvalidOperationException(
                    $"SPOT actor join request '{descriptor.RequestType}' is already registered on '{spot.GetType()}'.");
            }
        }
    }

    public bool TryResolve(Type requestType, out ZLinkSpotActorJoinDescriptor? descriptor)
    {
        return _descriptorsByRequestType.TryGetValue(requestType, out descriptor);
    }

    public bool TryResolveByName(string messageName, out ZLinkSpotActorJoinDescriptor? descriptor)
    {
        foreach (var entry in _descriptorsByRequestType.Values)
        {
            if (string.Equals(entry.MessageName, messageName, StringComparison.Ordinal))
            {
                descriptor = entry;
                return true;
            }
        }

        descriptor = null;
        return false;
    }
}
