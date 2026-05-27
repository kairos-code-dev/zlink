using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.E2ETests.Channels;
public sealed class DealerMeshTests
{
    [Fact]
    public async Task DealerMeshClient_Request_And_Send_Use_Registered_DealerMesh_Channel()
    {
        var meshEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        await using var context = new Context();
        await using var router = new RouterSocket(context);
        router.Bind(meshEndpoint);

        using var stopRouter = new CancellationTokenSource();
        var recorder = new MeshProfileRecorder();
        var routerTask = Task.Run(
            () => ChannelSupport.RunMeshRouterAsync(router, recorder, stopRouter.Token),
            stopRouter.Token);

        var clientBuilder = Host.CreateApplicationBuilder();
        clientBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddDealerMeshChannel("mesh", channel =>
            {
                channel.EnableClient(client =>
                {
                    client.UseManualConnections(connections => connections.Connect(meshEndpoint));
                });
            });
        });

        using var clientHost = clientBuilder.Build();
        await clientHost.StartAsync();

        var client = clientHost.Services.GetRequiredService<IZLinkChannelClient>();
        var reply = await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
            async () => await client
                .RequestChannel("mesh", new MeshProfileRequest { UserId = "mesh-request" })
                .Timeout(TimeSpan.FromMilliseconds(500))
                .SubmitAsync<MeshProfileReply>(),
            static result => result.Name == "mesh:mesh-request");

        Assert.Equal("mesh:mesh-request", reply.Name);

        await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
            async () =>
            {
                await client
                    .SendChannel("mesh", new MeshProfileRequest { UserId = "mesh-send" })
                    .Submit();
                await Task.Yield();
                return recorder.Commands.Count;
            },
            count => count > 0);

        Assert.Contains("mesh-request", recorder.Requests.ToArray());
        Assert.Contains("mesh-send", recorder.Commands.ToArray());

        await clientHost.StopAsync();
        stopRouter.Cancel();
        await routerTask.WaitAsync(TimeSpan.FromSeconds(5));
    }
}
