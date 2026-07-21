namespace Zlink.Framework.Runtime.Locations;

internal static class ZLinkRoutingIdSlotAllocationValidator
{
    internal static void ValidateAcquire(ZLinkRoutingIdSlotAcquireRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);
        ValidateGroupName(request.GroupName);
        if (request.SlotCount < 1)
            throw new ArgumentOutOfRangeException(nameof(request), "SlotCount must be greater than zero.");
        if (string.IsNullOrWhiteSpace(request.OwnerId))
            throw new ArgumentException("OwnerId must not be empty.", nameof(request));
        if (request.LeaseTtl <= TimeSpan.Zero)
            throw new ArgumentOutOfRangeException(nameof(request), "LeaseTtl must be greater than zero.");
        if (request.Members is null || request.Members.Count == 0)
            throw new ArgumentException("At least one allocation member is required.", nameof(request));

        var meshNames = new HashSet<string>(StringComparer.Ordinal);
        foreach (var member in request.Members)
        {
            if (member is null || string.IsNullOrWhiteSpace(member.MeshName))
                throw new ArgumentException("Allocation member mesh names must not be empty.", nameof(request));
            if (string.IsNullOrWhiteSpace(member.RoutingIdPrefix))
                throw new ArgumentException("Allocation member routing-id prefixes must not be empty.", nameof(request));
            if (!meshNames.Add(member.MeshName))
                throw new ArgumentException(
                    $"Allocation member mesh '{member.MeshName}' is duplicated.",
                    nameof(request));
        }
    }

    internal static void ValidateRelease(string groupName, int slot, ZLinkLocationOwnerToken owner)
    {
        ValidateGroupName(groupName);
        if (slot < 1) throw new ArgumentOutOfRangeException(nameof(slot));
        if (string.IsNullOrWhiteSpace(owner.OwnerId))
            throw new ArgumentException("OwnerId must not be empty.", nameof(owner));
        if (owner.Generation < 1)
            throw new ArgumentOutOfRangeException(nameof(owner), "Owner generation must be greater than zero.");
    }

    internal static void ValidateGroupName(string groupName)
    {
        if (string.IsNullOrWhiteSpace(groupName))
            throw new ArgumentException("Allocation group name must not be empty.", nameof(groupName));
    }
}
