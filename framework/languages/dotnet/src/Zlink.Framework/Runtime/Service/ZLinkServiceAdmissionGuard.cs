using Systems.Zlink.Framework.Runtime.Protocol;

namespace Zlink.Framework.Runtime.Service;

internal enum ZLinkServiceAdmissionDecision
{
    Accept = 1,
    Idempotent,
    Reject
}

internal static class ZLinkServiceAdmissionGuard
{
    private static readonly HashSet<byte> MutableExtensionFields =
        [1, 5, 8, 9, 10, 11, 12];

    internal static ZLinkServiceAdmissionDecision Evaluate(
        ZLinkServiceWireCodec.AdmissionRecord? current,
        ServiceWireConstants.Command command,
        ZLinkServiceWireCodec.AdmissionRecord incoming)
    {
        if (current is null)
            return command == ServiceWireConstants.Command.Update
                ? ZLinkServiceAdmissionDecision.Reject
                : ZLinkServiceAdmissionDecision.Accept;

        var existing = current.Value;
        if (incoming.LifecycleGeneration != existing.LifecycleGeneration)
            return command == ServiceWireConstants.Command.Update
                ? ZLinkServiceAdmissionDecision.Reject
                : ZLinkServiceAdmissionDecision.Accept;

        if (incoming.DescriptorRevision < existing.DescriptorRevision)
            return ZLinkServiceAdmissionDecision.Reject;
        if (incoming.DescriptorRevision == existing.DescriptorRevision)
            return incoming.DescriptorBytes.AsSpan().SequenceEqual(existing.DescriptorBytes)
                ? ZLinkServiceAdmissionDecision.Idempotent
                : ZLinkServiceAdmissionDecision.Reject;
        if (command != ServiceWireConstants.Command.Update)
            return ZLinkServiceAdmissionDecision.Reject;

        return ImmutableFieldsMatch(existing, incoming)
            ? ZLinkServiceAdmissionDecision.Accept
            : ZLinkServiceAdmissionDecision.Reject;
    }

    private static bool ImmutableFieldsMatch(
        ZLinkServiceWireCodec.AdmissionRecord existing,
        ZLinkServiceWireCodec.AdmissionRecord incoming)
    {
        if (!string.Equals(existing.MeshName, incoming.MeshName, StringComparison.Ordinal)
            || !string.Equals(
                existing.SecurityIdentity,
                incoming.SecurityIdentity,
                StringComparison.Ordinal)
            || existing.EffectiveMaxMessageBytes != incoming.EffectiveMaxMessageBytes
            || !string.Equals(
                existing.AdvertisedEndpoint,
                incoming.AdvertisedEndpoint,
                StringComparison.Ordinal)
            || existing.ObjectRole != incoming.ObjectRole
            || existing.ApplicationVersion != incoming.ApplicationVersion
            || existing.Channels.Count != incoming.Channels.Count
            || existing.ExtensionFields.Count != incoming.ExtensionFields.Count)
            return false;

        foreach (var channel in existing.Channels.Keys)
            if (!incoming.Channels.ContainsKey(channel))
                return false;

        foreach (var (id, value) in existing.ExtensionFields)
        {
            if (MutableExtensionFields.Contains(id))
                continue;
            if (!incoming.ExtensionFields.TryGetValue(id, out var candidate)
                || !value.AsSpan().SequenceEqual(candidate))
                return false;
        }

        return incoming.ExtensionFields.Keys.All(
            id => MutableExtensionFields.Contains(id)
                  || existing.ExtensionFields.ContainsKey(id));
    }
}
