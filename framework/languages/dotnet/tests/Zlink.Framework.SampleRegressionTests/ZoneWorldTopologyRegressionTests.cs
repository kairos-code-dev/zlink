using Xunit;

namespace Zlink.Framework.SampleRegressionTests;

public sealed partial class RegressionTests
{
    [Fact]
    public void ZoneWorld_Uses_One_Physical_Mesh_Per_Mesh_Participant()
    {
        var sampleRoot = ResolveSampleRoot("ZoneWorld");
        var participants = new[]
        {
            Path.Combine(sampleRoot, "Server", "Gateway", "Program.cs"),
            Path.Combine(sampleRoot, "Server", "Ops", "Program.cs"),
            Path.Combine(sampleRoot, "Server", "ZoneNode", "Program.cs")
        };

        foreach (var participant in participants)
        {
            var source = File.ReadAllText(participant);
            Assert.Equal(1, source.Split("AddRouteMesh(", StringSplitOptions.None).Length - 1);
            Assert.Contains("AddRouteMesh(ZoneWorldNames.MeshName)", source, StringComparison.Ordinal);
            Assert.DoesNotContain("AddRequestHandler<", source, StringComparison.Ordinal);
            Assert.DoesNotContain("AddSendHandler<", source, StringComparison.Ordinal);
        }

        var zoneNode = File.ReadAllText(participants[2]);
        var ops = File.ReadAllText(participants[1]);
        Assert.Contains("AddHandlerGroup(HandlerGroups.ZoneOps)", zoneNode, StringComparison.Ordinal);
        Assert.Contains("AddHandlerGroup(HandlerGroups.ZoneActors)", zoneNode, StringComparison.Ordinal);
        Assert.Contains("AddHandlerGroup(HandlerGroups.Ops)", ops, StringComparison.Ordinal);

        // The third ZoneNode role is the documented classic pub/sub-only
        // subscriber. It returns before RouteMesh configuration and is not a
        // second physical mesh exception.
        Assert.Contains("if (!hostsZones)", zoneNode, StringComparison.Ordinal);
        Assert.Contains("options.AddFanoutChannel", zoneNode, StringComparison.Ordinal);
    }
}
