namespace Zlink.Framework.Runtime.Configuration;

internal static partial class ZLinkFrameworkRegistrationValidator
{
    private static void ValidateLocations(ZLinkFrameworkRegistration registration)
    {
        var locations = registration.Locations;
        if (locations.StoreInstance is not null && locations.UseInMemoryStores)
        {
            throw new ZLinkConfigurationException(
                "AddLocationStore registers every store role at once and cannot be combined with "
                + "UseTestLocationStore.");
        }

        var allocations = ZLinkRoutingIdAllocationCatalog.Collect(registration);
        if (allocations.Count == 0) return;

        locations.Options.AllocatedRoutingIdsEnabled = true;

        if (!locations.Enabled)
            throw new ZLinkConfigurationException(
                "Allocated routing ids require AddLocationStore(...) or UseTestLocationStore().");
        if (locations.StoreInstance is not null
            && locations.StoreInstance is not IZLinkRoutingIdSlotAllocationStore)
            throw new ZLinkConfigurationException(
                "The registered location store does not provide routing-id slot allocation.");

        ValidateAllocationTimes(locations.Options);
        foreach (var allocation in allocations) ValidateAllocation(allocation);

        foreach (var group in allocations.GroupBy(static allocation => allocation.GroupName, StringComparer.Ordinal))
        {
            var expectedCount = group.First().SlotCount;
            if (group.Any(allocation => allocation.SlotCount != expectedCount))
                throw new ZLinkConfigurationException(
                    $"Routing-id allocation group '{group.Key}' must use one slot count for every member.");
            var duplicate = group.GroupBy(static allocation => allocation.MemberName, StringComparer.Ordinal)
                .FirstOrDefault(static members => members.Count() > 1);
            if (duplicate is not null)
                throw new ZLinkConfigurationException(
                    $"Routing-id allocation group '{group.Key}' contains duplicate member '{duplicate.Key}'.");
        }
    }

    private static void ValidateAllocationTimes(ZLinkLocationOptions options)
    {
        if (options.HeartbeatInterval <= TimeSpan.Zero
            || options.OwnerLeaseTtl <= TimeSpan.Zero
            || options.RoutingIdFencingMargin <= TimeSpan.Zero
            || options.OwnerLeaseRenewTimeout <= TimeSpan.Zero)
            throw new ZLinkConfigurationException(
                "Allocated routing-id lease times must all be greater than zero.");

        if (options.HeartbeatInterval + options.OwnerLeaseRenewTimeout
            >= options.OwnerLeaseTtl - options.RoutingIdFencingMargin)
            throw new ZLinkConfigurationException(
                "Allocated routing-id lease times must satisfy HeartbeatInterval + "
                + "OwnerLeaseRenewTimeout < OwnerLeaseTtl - RoutingIdFencingMargin.");
    }

    private static void ValidateAllocation(ZLinkRoutingIdAllocationMemberRegistration allocation)
    {
        if (allocation.SlotCount < 1)
            throw new ZLinkConfigurationException(
                $"Routing-id allocation group '{allocation.GroupName}' must configure at least one slot.");
        if (string.IsNullOrWhiteSpace(allocation.GroupName))
            throw new ZLinkConfigurationException("Routing-id allocation group name must not be empty.");
        if (string.IsNullOrWhiteSpace(allocation.Prefix))
            throw new ZLinkConfigurationException(
                $"Routing-id allocation member '{allocation.MemberName}' must define a prefix.");

        try
        {
            _ = RoutingId.From(allocation.Prefix + allocation.SlotCount.ToString(
                System.Globalization.CultureInfo.InvariantCulture));
        }
        catch (Exception error) when (error is ArgumentException or InvalidOperationException)
        {
            throw new ZLinkConfigurationException(
                $"Routing-id allocation member '{allocation.MemberName}' can produce a routing id outside the 1..255 byte contract: {error.Message}");
        }

        if (allocation.HasExplicitRoutingId)
            throw new ZLinkConfigurationException(
                $"Routing-id allocation member '{allocation.MemberName}' cannot combine SetRoutingId and UseAllocatedRoutingId.");
        if (allocation.HasExplicitEntrySpotRoutingId)
            throw new ZLinkConfigurationException(
                $"SPOT node '{allocation.MemberName}' cannot combine allocated routing id and explicit entry Spot routing id.");
        if (!allocation.HasBindableRole)
            throw new ZLinkConfigurationException(
                $"Routing-id allocation member '{allocation.MemberName}' must enable a socket or SPOT node role.");
    }
}

internal sealed record ZLinkRoutingIdAllocationMemberRegistration(
    string GroupName,
    string MemberName,
    string Prefix,
    int SlotCount,
    bool HasExplicitRoutingId,
    bool HasExplicitEntrySpotRoutingId,
    bool HasBindableRole,
    Action<RoutingId> Apply);

internal static class ZLinkRoutingIdAllocationCatalog
{
    internal static IReadOnlyList<ZLinkRoutingIdAllocationMemberRegistration> Collect(
        ZLinkFrameworkRegistration registration)
    {
        var members = new List<ZLinkRoutingIdAllocationMemberRegistration>();
        foreach (var channel in registration.Channels.Values)
            if (channel.RoutingIdAllocation is { } allocation)
                members.Add(Create(
                    allocation,
                    channel.ChannelName,
                    channel.HasExplicitRoutingId,
                    false,
                    channel.Publisher is not null || channel.Subscriber is not null,
                    routingId => channel.RoutingId = routingId));

        foreach (var spot in registration.SpotNodes.Values)
            if (spot.RoutingIdAllocation is { } allocation)
                members.Add(Create(
                    allocation,
                    spot.SpotNodeName,
                    spot.HasExplicitRoutingId,
                    spot.HasExplicitEntrySpotRoutingId,
                    spot.Router is not null,
                    routingId =>
                    {
                        spot.RoutingId = routingId;
                        spot.EntrySpotOptions.RoutingId = routingId;
                    }));

        return members;
    }

    private static ZLinkRoutingIdAllocationMemberRegistration Create(
        ZLinkRoutingIdAllocationRegistration allocation,
        string memberName,
        bool hasExplicitRoutingId,
        bool hasExplicitEntrySpotRoutingId,
        bool hasBindableRole,
        Action<RoutingId> apply) => new(
        allocation.GroupName ?? memberName,
        memberName,
        allocation.RoutingIdPrefix,
        allocation.SlotCount,
        hasExplicitRoutingId,
        hasExplicitEntrySpotRoutingId,
        hasBindableRole,
        apply);
}
