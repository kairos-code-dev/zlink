using Zlink.Framework.Runtime.Channels;
using Zlink.Framework.Runtime.Configuration;
using Zlink.Framework.Runtime.Configuration.Builders;

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
            .ChannelName("route")
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
            () => mesh.ChannelName("route").SetWeight(weight));
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

    private sealed record WeightedCandidate(string Name, int Weight);
}
