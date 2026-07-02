using ShoppingMall.Server.Configuration;
using ShoppingMall.Server.OrderWorkflow.Application.OrderWorkflow;
using ShoppingMall.Server.OrderWorkflow.Infrastructure.ZLink.Handlers;
using ShoppingMall.Server.OrderWorkflow.Infrastructure.ZLink.Spots.OrderWorkflowSpot;
using ShoppingMall.Server.Shared.Ports.Outbound;
using ShoppingMall.Server.Shared.Store;
using ShoppingMall.Shared.Contracts;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Locations.Redis;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Samples.Logging;

namespace ShoppingMall.Server.OrderWorkflow;

internal static class Program
{
    public static async Task Main(string[] args)
    {
        var instanceId = ReadOption(args, "--instance")
                         ?? Environment.GetEnvironmentVariable("SHOPPINGMALL_WORKFLOW_INSTANCE")
                         ?? "workflow-a";
        var topology = SampleTopology.Create();
        var instance = topology.ForWorkflowInstance(instanceId);
        var builder = WebApplication.CreateBuilder(args);
        SampleLogging.Configure(
            builder.Logging,
            SampleLogging.DirectoryFromEnvironment("SHOPPINGMALL_LOG_DIR"),
            instance.InstanceId);

        builder.WebHost.UseUrls(instance.HttpUrl);
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton(instance);
        builder.Services.AddSingleton(new FileCommerceStores(topology.StoreDirectory));
        builder.Services.AddSingleton<IOrderEventStore>(static provider =>
            provider.GetRequiredService<FileCommerceStores>());
        builder.Services.AddSingleton<IOrderReadModelStore>(static provider =>
            provider.GetRequiredService<FileCommerceStores>());
        builder.Services.AddSingleton<ICommerceStateStore>(static provider =>
            provider.GetRequiredService<FileCommerceStores>());
        builder.Services.AddSingleton<OrderWorkflowService>();

        builder.Services.AddZLinkFramework(options =>
        {
            options.AddRedisLocationStore(redis =>
            {
                redis.ConnectionString = topology.RedisEndpoint;
                redis.KeyPrefix = topology.RedisKeyPrefix;
            });
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path(instance.InstanceId))
                .TraceLabel(instance.InstanceId);
            options.AddRouteMesh(SampleNames.OrderWorkflowRouteChannel)
                .EnableServer(instance.RouteEndpoint)
                .SetRoutingId(instance.RouteRid)
                .AddRequestHandler<StartOrderWorkflowRouteHandler, StartOrderWorkflowReq, StartOrderWorkflowRes>()
                .AddRequestHandler<ContinueOrderWorkflowRouteHandler, ContinueOrderWorkflowReq,
                    ContinueOrderWorkflowRes>()
                .AddRequestHandler<RebuildOrderProjectionRouteHandler, RebuildOrderProjectionReq,
                    RebuildOrderProjectionRes>();
            options.AddSpotMesh(SampleNames.OrderSpotDiscovery)
                .EnableRouter(instance.SpotRouterEndpoint)
                .SetRoutingId(instance.SpotRid)
                .EnablePubSub(instance.SpotEndpoint)
                .AddSpotFactory<OrderWorkflowSpot>();
        });

        var app = builder.Build();
        app.MapGet("/health", () => Results.Ok(new { ready = true, instance = instance.InstanceId }));
        await app.RunAsync();
    }

    private static string? ReadOption(string[] args, string name)
    {
        var index = Array.IndexOf(args, name);
        return index >= 0 && index + 1 < args.Length ? args[index + 1] : null;
    }
}
