using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotPacketRegistry
{
    private readonly List<ZLinkSpotPacketRegistration> _registrations = [];
    private readonly Dictionary<string, ZLinkSpotDescriptor> _descriptorsByName = new(StringComparer.Ordinal);

    public bool HasPackets => _descriptorsByName.Count > 0;

    public void Add(Type handlerType)
    {
        Add(handlerType, packetName: null);
    }

    public void Add(Type handlerType, string? packetName)
    {
        _registrations.Add(new ZLinkSpotPacketRegistration(handlerType, packetName));
    }

    public void Bind(object spot)
    {
        foreach (var packet in _registrations)
        {
            var descriptor = ZLinkSpotDescriptorFactory.CreatePacketDescriptor(
                packet.HandlerType,
                spot.GetType(),
                packet.PacketName);
            _descriptorsByName.Add(descriptor.MessageName, descriptor);
        }
    }

    public bool TryResolve(ZLinkEnvelopeHeader header, out ZLinkSpotDescriptor? descriptor)
    {
        return _descriptorsByName.TryGetValue(header.MessageName, out descriptor);
    }
}
