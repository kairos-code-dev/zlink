using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.UnitTests;

public sealed class ActorClientTests
{
    [Fact]
    public void AddZLinkFramework_Registers_ActorClient_With_SpotNode_And_Locations()
    {
        var services = new ServiceCollection();
        services.AddZLinkFramework(options =>
        {
            options.UseInMemoryLocationStores();
            options.AddSpotMesh("play").EnableRouter("inproc://actor-client");
        });

        using var provider = services.BuildServiceProvider();

        Assert.NotNull(provider.GetService<IZLinkActorClient>());
    }

    [Fact]
    public void AddZLinkFramework_DoesNot_Register_ActorClient_Without_Locations()
    {
        var services = new ServiceCollection();
        services.AddZLinkFramework(options =>
        {
            options.AddSpotMesh("play").EnableRouter("inproc://actor-client");
        });

        using var provider = services.BuildServiceProvider();

        Assert.Null(provider.GetService<IZLinkActorClient>());
    }

    [Fact]
    public void ActorSendCall_Has_No_Submit_Terminal()
    {
        Assert.Empty(typeof(IZLinkActorSendCall).GetMethods().Where(static method => method.Name == "Submit"));
    }

    [Fact]
    public void ActorRequestCall_Has_No_Submit_Terminal()
    {
        Assert.Empty(typeof(IZLinkActorRequestCall).GetMethods().Where(static method => method.Name == "Submit"));
    }
}
