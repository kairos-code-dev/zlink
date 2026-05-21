using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.E2ETests;

public sealed class EmbeddedRegistryTests
{
    [Fact]
    public async Task EmbeddedRegistry_Query_Service_Resolves_And_Reads_Status()
    {
        var pubEndpoint = $"tcp://127.0.0.1:{GetEphemeralPort()}";
        var routerEndpoint = $"tcp://127.0.0.1:{GetEphemeralPort()}";
        var builder = Host.CreateApplicationBuilder();

        builder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = pubEndpoint;
            options.RouterEndpoint = routerEndpoint;
            options.RegistryId = 7;
        });

        using var host = builder.Build();
        await host.StartAsync();

        var query = host.Services.GetRequiredService<IZLinkRegistryQuery>();
        var status = await ExecuteWithRetryAsync(
            () => query.StatusSnapshotAsync().AsTask(),
            result => result.RegistryId == 7);

        Assert.Equal(7u, status.RegistryId);
        Assert.Equal(routerEndpoint, status.BindEndpoint);

        await host.StopAsync();
    }

    [Fact]
    public async Task RemoteRegistryQueryClient_Can_Read_Topology_Snapshot()
    {
        var pubEndpoint = $"tcp://127.0.0.1:{GetEphemeralPort()}";
        var routerEndpoint = $"tcp://127.0.0.1:{GetEphemeralPort()}";

        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = pubEndpoint;
            options.RouterEndpoint = routerEndpoint;
        });

        var queryBuilder = Host.CreateApplicationBuilder();
        queryBuilder.Services.AddZLinkRegistryQueryClient(options =>
        {
            options.Endpoint = routerEndpoint;
        });

        using var registryHost = registryBuilder.Build();
        using var queryHost = queryBuilder.Build();

        await registryHost.StartAsync();
        await queryHost.StartAsync();

        var queryClient = queryHost.Services.GetRequiredService<IZLinkRegistryQueryClient>();
        var snapshot = await ExecuteWithRetryAsync(
            () => queryClient.SnapshotAsync().AsTask(),
            static result => result is not null);

        Assert.NotNull(snapshot);

        await queryHost.StopAsync();
        await registryHost.StopAsync();
    }

    private static async Task<T> ExecuteWithRetryAsync<T>(
        Func<Task<T>> action,
        Func<T, bool> predicate,
        int attempts = 20,
        int delayMs = 100)
    {
        Exception? lastError = null;

        for (var attempt = 0; attempt < attempts; attempt++)
        {
            try
            {
                var result = await action();
                if (predicate(result))
                {
                    return result;
                }
            }
            catch (Exception ex)
            {
                lastError = ex;
            }

            await Task.Delay(delayMs);
        }

        if (lastError is not null)
        {
            throw lastError;
        }

        throw new TimeoutException("Registry integration retry timed out.");
    }

    private static int GetEphemeralPort()
    {
        return ChannelMessagingTestSupport.GetEphemeralPort();
    }
}
