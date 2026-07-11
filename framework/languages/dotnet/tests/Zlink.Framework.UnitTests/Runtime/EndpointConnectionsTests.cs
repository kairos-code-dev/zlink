using Zlink.Framework.Runtime.Configuration;

namespace Zlink.Framework.UnitTests;

public sealed class EndpointConnectionsTests
{
    [Fact]
    public void Configured_Endpoints_Are_Replayed_And_Runtime_Changes_Are_Applied()
    {
        var connections = new ZLinkEndpointConnections();
        var connected = new List<string>();
        var disconnected = new List<string>();

        connections.Connect("tcp://127.0.0.1:7101");
        connections.Connect("tcp://127.0.0.1:7101");
        connections.Attach(connected.Add, disconnected.Add);
        connections.Connect("tcp://127.0.0.1:7102");
        connections.Disconnect("tcp://127.0.0.1:7101");

        Assert.Equal(
            ["tcp://127.0.0.1:7101", "tcp://127.0.0.1:7102"],
            connected);
        Assert.Equal(["tcp://127.0.0.1:7101"], disconnected);
        Assert.Equal(["tcp://127.0.0.1:7102"], connections.ListConnections());
    }

    [Fact]
    public void New_Runtime_Generation_Replaces_Callbacks_And_Replays_Current_Endpoints()
    {
        var connections = new ZLinkEndpointConnections();
        var firstGeneration = new List<string>();
        var secondGeneration = new List<string>();
        connections.Connect("tcp://127.0.0.1:7201");

        connections.Attach(firstGeneration.Add, _ => { });
        connections.Attach(secondGeneration.Add, _ => { });
        connections.Connect("tcp://127.0.0.1:7202");

        Assert.Equal(["tcp://127.0.0.1:7201"], firstGeneration);
        Assert.Equal(
            ["tcp://127.0.0.1:7201", "tcp://127.0.0.1:7202"],
            secondGeneration);
    }

    [Fact]
    public void Runtime_Handle_Does_Not_Expose_Mutable_List_Operations()
    {
        IZLinkEndpointConnections connections = new ZLinkEndpointConnections();

        Assert.False(connections is IList<string>);
        Assert.True(connections is IReadOnlyCollection<string>);
    }
}
