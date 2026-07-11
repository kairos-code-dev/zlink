namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Owns the framework-reserved peer capability encoding. Actor placement
/// compares the configured actor type, not a language runtime type name, so
/// the same row remains meaningful across framework implementations.
/// </summary>
internal static class ZLinkPeerCapabilities
{
    private const string ActorTypePrefix = "actor:";

    internal static IReadOnlyList<string>? ActorTypes(IEnumerable<string> actorTypes)
    {
        var capabilities = actorTypes
            .Select(static actorType => ActorTypePrefix + actorType)
            .Distinct(StringComparer.Ordinal)
            .Order(StringComparer.Ordinal)
            .ToArray();
        return capabilities.Length == 0 ? null : capabilities;
    }

    internal static bool SupportsActorType(ZLinkPeerLocation peer, string actorType) =>
        peer.Capabilities?.Contains(ActorTypePrefix + actorType, StringComparer.Ordinal) == true;
}
