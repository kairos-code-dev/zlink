using Microsoft.Extensions.Configuration;

using ShoppingMall.Server.Configuration;
using ShoppingMall.Server.OrderWorkflow.Application.OrderWorkflow;
using ShoppingMall.Server.OrderWorkflow.Application.SelfCheck;
using ShoppingMall.Server.OrderWorkflow.Infrastructure.ZLink.Handlers;
using ShoppingMall.Server.OrderWorkflow.Infrastructure.ZLink.Spots.OrderWorkflowSpot;
using ShoppingMall.Server.Shared.Ports.Outbound;
using ShoppingMall.Server.Shared.Store;
using ShoppingMall.Shared.Contracts;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Locations.Redis;
using Zlink.Samples.Logging;

namespace ShoppingMall.Server.OrderWorkflow;

public static class OrderWorkflowServerHostFactory
{
    public static WebApplication Build(
        SampleTopology topology,
        WorkflowInstanceTopology instance,
        string logDirectory,
        string[]? args = null)
    {
        var builder = WebApplication.CreateBuilder(args ?? []);
        builder.Configuration.Sources.Clear();
        builder.Configuration.AddInMemoryCollection();
        SampleLogging.Configure(
            builder.Logging,
            logDirectory,
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
                .TraceLogFile(SampleFlowLog.Path(logDirectory, instance.InstanceId))
                .TraceLabel(instance.InstanceId);
            options.AddHandlersFromAssemblyOf(typeof(OrderWorkflowServerHostFactory));
            options.AddClientServerChannel(SampleNames.OrderWorkflowChannelFor(instance.InstanceId))
                .EnableServer(instance.ChannelEndpoint)
                .SetRoutingId(instance.RouteRid)
                .AddHandlerGroup("order-workflow");
            var mesh1 = options.AddRouteMesh(SampleNames.OrderWorkflowRouteChannel)
                .UseDrainPolicy(ZLinkMeshNodeDrainPolicy.ReleaseAndRecreate)
                .Listen(instance.SpotRouterEndpoint)
                .SetRoutingId(instance.SpotRid)
                .AddSpotFactory<OrderWorkflowSpot>();
            mesh1.ChannelName(SampleNames.OrderWorkflowRouteChannel);
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
        app.MapPost("/self-check/projection/{orderId}/delete", async (
            string orderId,
            IOrderReadModelStore readModels,
            CancellationToken cancellationToken) =>
        {
            await readModels.DeleteAsync(orderId, cancellationToken);
            return Results.Ok();
        });
        app.MapPost("/self-check/projection/{orderId}/rebuild", async (
            string orderId,
            IZLinkSpotManager spots,
            IZLinkRouteClient routes,
            IZLinkSpotHandleResolver spotHandles,
            CancellationToken cancellationToken) =>
        {
            var address = await StartOrderWorkflowRouteHandler.EnsureSpotAsync(
                spots,
                spotHandles,
                orderId,
                cancellationToken);
            var response = await routes
                .RequestToSpot(address, new RebuildOrderProjectionReq(orderId))
                .Async<RebuildOrderProjectionRes>(cancellationToken);
            return Results.Ok(response);
        });
        return app;
    }
}
