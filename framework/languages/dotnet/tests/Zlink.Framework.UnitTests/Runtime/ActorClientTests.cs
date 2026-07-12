using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.UnitTests;

public sealed class ActorClientTests
{
    [Fact]
    public async Task AddZLinkFramework_Registers_ActorClient_With_SpotNode_And_Locations()
    {
        var services = new ServiceCollection();
        services.AddZLinkFramework(options =>
        {
            options.UseInMemoryLocationStores();
            options.AddSpotMesh("play").EnableRouter("inproc://actor-client");
        });

        await using var provider = services.BuildServiceProvider();

        Assert.NotNull(provider.GetService<IZLinkActorClient>());
    }

    [Fact]
    public async Task AddZLinkFramework_DoesNot_Register_ActorClient_Without_Locations()
    {
        var services = new ServiceCollection();
        services.AddZLinkFramework(options =>
        {
            options.AddSpotMesh("play").EnableRouter("inproc://actor-client");
        });

        await using var provider = services.BuildServiceProvider();

        Assert.Null(provider.GetService<IZLinkActorClient>());
    }

    [Fact]
    public async Task AddZLinkFramework_DoesNot_Register_ActorClient_For_PubSub_Only_SpotNode()
    {
        var services = new ServiceCollection();
        services.AddZLinkFramework(options =>
        {
            options.UseInMemoryLocationStores();
            options.AddSpotMesh("play").EnablePubSub("inproc://actor-client-pubsub");
        });

        await using var provider = services.BuildServiceProvider();

        Assert.Null(provider.GetService<IZLinkActorClient>());
    }

    [Fact]
    public void ActorSendCall_Has_One_Void_Submit_Terminal()
    {
        var submit = Assert.Single(
            typeof(IZLinkActorSendCall).GetMethods(),
            static method => method.Name == "Submit");
        Assert.Equal(typeof(void), submit.ReturnType);
        var cancellation = Assert.Single(submit.GetParameters());
        Assert.Equal(typeof(CancellationToken), cancellation.ParameterType);
        Assert.True(cancellation.HasDefaultValue);
    }

    [Fact]
    public void ActorRequestCall_Has_No_Submit_Terminal()
    {
        Assert.Empty(typeof(IZLinkActorRequestCall).GetMethods().Where(static method => method.Name == "Submit"));
    }
}
