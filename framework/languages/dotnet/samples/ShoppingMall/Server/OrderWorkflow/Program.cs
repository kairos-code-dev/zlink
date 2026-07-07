using ShoppingMall.Server.Configuration;
using ShoppingMall.Server.OrderWorkflow.Application.OrderWorkflow;
using ShoppingMall.Server.OrderWorkflow.Application.SelfCheck;
using ShoppingMall.Server.OrderWorkflow.Infrastructure.ZLink.Handlers;
using ShoppingMall.Server.OrderWorkflow.Infrastructure.ZLink.Spots.OrderWorkflowSpot;
using ShoppingMall.Server.Shared.Ports.Outbound;
using ShoppingMall.Server.Shared.Store;
using ShoppingMall.Shared.Contracts;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Locations.Redis;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Contracts.Spots;
using Zlink.Samples.Logging;
using Systems.Zlink;

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
        builder.Services.AddSingleton(new RedisCommerceStores(topology));
        builder.Services.AddSingleton<IOrderEventStore>(static provider =>
            provider.GetRequiredService<RedisCommerceStores>());
        builder.Services.AddSingleton<IOrderReadModelStore>(static provider =>
            provider.GetRequiredService<RedisCommerceStores>());
        builder.Services.AddSingleton<ICommerceStateStore>(static provider =>
            provider.GetRequiredService<RedisCommerceStores>());
        builder.Services.AddSingleton<OrderWorkflowService>();
        builder.Services.AddSingleton<OrderWorkflowSelfCheckService>();

        builder.Services.AddZLinkFramework(options =>
        {
            options.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(topology.RedisEndpoint)
                .SetKeyPrefix(topology.RedisKeyPrefix)));
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path(instance.InstanceId))
                .TraceLabel(instance.InstanceId);
            options.AddClientServerChannel(SampleNames.OrderWorkflowChannelFor(instance.InstanceId))
                .EnableServer(instance.ChannelEndpoint)
                .SetRoutingId(instance.RouteRid)
                .AddRequestHandler<StartOrderWorkflowRouteHandler, StartOrderWorkflowReq, StartOrderWorkflowRes>()
                .AddRequestHandler<ContinueOrderWorkflowRouteHandler, ContinueOrderWorkflowReq,
                    ContinueOrderWorkflowRes>()
                .AddRequestHandler<RebuildOrderProjectionRouteHandler, RebuildOrderProjectionReq,
                    RebuildOrderProjectionRes>();
            options.AddSpotMesh(SampleNames.OrderWorkflowRouteChannel)
                .EnableRouter(instance.SpotRouterEndpoint)
                .SetRoutingId(instance.SpotRid)
                .EnablePubSub(instance.SpotEndpoint)
                .AddSpotFactory<OrderWorkflowSpot>();
        });

        var app = builder.Build();
        app.Use(async (context, next) =>
        {
            try
            {
                await next(context);
            }
            catch (Exception error)
            {
                app.Services.GetRequiredService<ILoggerFactory>()
                    .CreateLogger("ShoppingMall.Server.OrderWorkflow")
                    .LogError(
                        error,
                        "shoppingmall workflow http handler failed: endpoint={Endpoint} error={Error}",
                        context.Request.Path.Value,
                        error.Message);
                throw;
            }
        });
        app.MapGet("/health", () => Results.Ok(new { ready = true, instance = instance.InstanceId }));
        app.MapPost("/self-check/workflow/inventory-reserved", async (
            StartOrderWorkflowReq request,
            IZLinkSpotManager spots,
            IZLinkRouteClient routes,
            WorkflowInstanceTopology instance,
            CancellationToken cancellationToken) =>
        {
            await spots.GetOrCreateAsync<OrderWorkflowSpot, OrderWorkflowSpotCreateReq>(
                RoutingId.From(request.OrderId),
                new OrderWorkflowSpotCreateReq(request.OrderId),
                cancellationToken);
            var address = new ZLinkSpotAddress(instance.SpotRid, RoutingId.From(request.OrderId));
            var response = await routes
                .RequestToSpot(SampleNames.OrderWorkflowRouteChannel, address, new PrepareInventoryReservedCheckpointReq(request))
                .Async<StartOrderWorkflowRes>(cancellationToken);
            return Results.Ok(response);
        });
        await app.RunAsync();
    }

    private static string? ReadOption(string[] args, string name)
    {
        var index = Array.IndexOf(args, name);
        return index >= 0 && index + 1 < args.Length ? args[index + 1] : null;
    }
}
