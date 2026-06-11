using Systems.Zlink.Codecs.Json;
using ShoppingMallCheckout.Server.OrderWorkflow.Adapters.ZLink.Handlers;
using ShoppingMallCheckout.Server.OrderWorkflow.Adapters.ZLink.Spots;
using ShoppingMallCheckout.Server.OrderWorkflow.Application.CheckoutWorkflow;
using ShoppingMallCheckout.Server.Shared.Ports.Outbound;
using ShoppingMallCheckout.Server.Shared.Store;
using ShoppingMallCheckout.Server.Configuration;
using ShoppingMallCheckout.Shared.Contracts;
using Zlink.Framework.AspNetCore;

namespace ShoppingMallCheckout.Server.OrderWorkflow;

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

        builder.WebHost.UseUrls(instance.HttpUrl);
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton(instance);
        builder.Services.AddSingleton(new FileCommerceStores(topology.StoreDirectory));
        builder.Services.AddSingleton<IOrderEventStore>(static provider => provider.GetRequiredService<FileCommerceStores>());
        builder.Services.AddSingleton<IOrderReadModelStore>(static provider => provider.GetRequiredService<FileCommerceStores>());
        builder.Services.AddSingleton<ICommerceStateStore>(static provider => provider.GetRequiredService<FileCommerceStores>());
        builder.Services.AddSingleton<OrderWorkflowService>();

        builder.Services.AddZLinkFramework(options =>
        {
            options.DefaultTimeout = SampleTimings.WorkflowTimeout;
            options.Codecs.AddJson();
            options.UseDiscovery(discovery => discovery.AddRegistryEndpoint(topology.RegistryRouterEndpoint));
            options.AddRouteMeshChannel(SampleNames.OrderWorkflowRouteChannel, route =>
            {
                route.Bind(instance.RouteEndpoint);
                route.ConfigureRouting(routing => routing.RoutingId = instance.RouteRid);
                route.AddRequestHandler<StartOrderWorkflowRouteHandler, StartOrderWorkflowReq, StartOrderWorkflowRes>();
                route.AddRequestHandler<ContinueOrderWorkflowRouteHandler, ContinueOrderWorkflowReq, ContinueOrderWorkflowRes>();
                route.AddRequestHandler<RebuildOrderProjectionRouteHandler, RebuildOrderProjectionReq, RebuildOrderProjectionRes>();
            });
            options.AddSpotMesh(SampleNames.OrderSpotDiscovery, mesh =>
            {
                mesh.AddNode(SampleNames.OrderSpotNode, spot =>
                {
                    spot.EnableRouter(router =>
                    {
                        router.BindRouter(instance.SpotRouterEndpoint);
                        router.SetRoutingId(instance.SpotRid);
                    });
                    spot.EnablePubSub(pubsub => pubsub.BindPubSub(instance.SpotEndpoint));
                    spot.AddSpotFactory<OrderWorkflowSpot>();
                });
            });
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
