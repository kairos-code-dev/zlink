using Systems.Zlink;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Runtime.Actors;
using Zlink.Framework.Runtime.Channels;
using Zlink.Framework.Runtime.Configuration;
using Zlink.Framework.Runtime.Configuration.Builders;
using Zlink.Framework.Runtime.Spots;

namespace Zlink.Framework.UnitTests.Runtime;

public sealed class WeightContractTests
{
    [Theory]
    [InlineData(0)]
    [InlineData(100)]
    [InlineData(10_000)]
    public void StartupBuildersAcceptTheSignedWeightRange(int weight)
    {
        var registration = new ZLinkFrameworkRegistration();
        var options = new ZLinkFrameworkOptionsBuilder(registration);

        options.AddRouteMesh("mesh")
            .SetPlacementWeight(weight)
            .Channel("route").Server()
            .SetWeight(weight);
        options.AddClientServerChannel("rpc")
            .Server()
            .SetWeight(weight);

        Assert.Equal(weight, registration.SpotNodes["mesh"].PlacementWeight);
        Assert.Equal(
            weight,
            registration.SpotNodes["mesh"].ChannelMemberships.Single().Weight);
        Assert.Equal(
            weight,
            registration.Channels["rpc"].Server!.SocketConfig.Weight);
    }

    [Theory]
    [InlineData(-1)]
    [InlineData(10_001)]
    public void StartupBuildersRejectWeightsOutsideTheSignedRange(int weight)
    {
        var registration = new ZLinkFrameworkRegistration();
        var options = new ZLinkFrameworkOptionsBuilder(registration);
        var mesh = options.AddRouteMesh("mesh");

        Assert.Throws<ZLinkConfigurationException>(
            () => mesh.SetPlacementWeight(weight));
        Assert.Throws<ZLinkConfigurationException>(
            () => mesh.Channel("route").Server().SetWeight(weight));
        Assert.Throws<ZLinkConfigurationException>(
            () => options.AddClientServerChannel("rpc")
                .Server()
                .SetWeight(weight));
    }

    [Fact]
    public void WeightedSelectionUsesA64BitSumAndExactRelativeWeights()
    {
        var overflowInt32 = Enumerable.Repeat(10_000, 214_749).ToArray();
        Assert.Equal(
            2_147_490_000L,
            ZLinkWeightedSelector.Sum(
                overflowInt32,
                static weight => weight));

        WeightedCandidate[] candidates =
        [
            new("one", 100),
            new("three", 300)
        ];
        var counts = candidates.ToDictionary(
            static candidate => candidate.Name,
            static _ => 0,
            StringComparer.Ordinal);
        long cursor = 0;
        for (var index = 0; index < 400; index++)
        {
            var selected = ZLinkWeightedSelector.Select(
                candidates,
                static candidate => candidate.Weight,
                ref cursor);
            counts[selected!.Name]++;
        }

        Assert.Equal(100, counts["one"]);
        Assert.Equal(300, counts["three"]);
    }

    [Fact]
    public void ObjectPlacementFiltersCapacityAndZeroWeightBeforeSelection()
    {
        var eligible = Descriptor(
            weight: 100,
            actors: new ZLinkPopulationCapacity(9, 0, 10),
            spots: new ZLinkPopulationCapacity(9, 0, 10),
            spotType: new ZLinkSpotTypeCapacity(
                ZLinkPlacementObjectKind.UserSpot,
                "room",
                9,
                0,
                10));
        Assert.True(ZLinkActorManagerService.IsEligibleCandidate(
            eligible,
            "player"));
        Assert.True(ZLinkSpotRuntimeManager.IsEligibleCandidate(
            eligible,
            "room"));

        var zeroWeight = eligible with { PlacementWeight = 0 };
        Assert.False(ZLinkActorManagerService.IsEligibleCandidate(
            zeroWeight,
            "player"));
        Assert.False(ZLinkSpotRuntimeManager.IsEligibleCandidate(
            zeroWeight,
            "room"));

        var actorFull = eligible with
        {
            Capacity = eligible.Capacity with
            {
                Actors = new ZLinkPopulationCapacity(9, 1, 10)
            }
        };
        Assert.False(ZLinkActorManagerService.IsEligibleCandidate(
            actorFull,
            "player"));

        var spotTypeFull = eligible with
        {
            Capacity = eligible.Capacity with
            {
                SpotTypes =
                [
                    new ZLinkSpotTypeCapacity(
                        ZLinkPlacementObjectKind.UserSpot,
                        "room",
                        9,
                        1,
                        10)
                ]
            }
        };
        Assert.False(ZLinkSpotRuntimeManager.IsEligibleCandidate(
            spotTypeFull,
            "room"));
    }

    private static ZLinkMeshNodeDescriptor Descriptor(
        int weight,
        ZLinkPopulationCapacity actors,
        ZLinkPopulationCapacity spots,
        ZLinkSpotTypeCapacity spotType) =>
        new(
            "objects",
            RoutingId.From("weight-target"),
            7,
            1,
            "inproc://weight-target",
            new Dictionary<string, int>(),
            "test",
            "owner",
            3,
            DateTimeOffset.UtcNow)
        {
            State = ZLinkFrameworkRuntimeState.Serving,
            ObjectRole = ZLinkMeshNodeObjectRole.Server,
            EntrySpotId = "weight-entry",
            PlacementWeight = weight,
            ObjectCapabilities =
            [
                new(
                    ZLinkPlacementObjectKind.Actor,
                    "player",
                    ZLinkObjectMaintenancePolicyKind.Disabled,
                    false,
                    0),
                new(
                    ZLinkPlacementObjectKind.UserSpot,
                    "room",
                    ZLinkObjectMaintenancePolicyKind.Disabled,
                    false,
                    0)
            ],
            Capacity = new(actors, spots, [spotType])
        };

    private sealed record WeightedCandidate(string Name, int Weight);
}
