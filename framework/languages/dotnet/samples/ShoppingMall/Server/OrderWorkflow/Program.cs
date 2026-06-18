using Zlink.Framework.Contracts.Codecs.Json;
using ShoppingMall.Server.OrderWorkflow.Adapters.ZLink.Handlers;
using ShoppingMall.Server.OrderWorkflow.Adapters.ZLink.Spots;
using ShoppingMall.Server.OrderWorkflow.Application.OrderWorkflow;
using ShoppingMall.Server.Shared.Ports.Outbound;
using ShoppingMall.Server.Shared.Store;
using ShoppingMall.Server.Configuration;
using ShoppingMall.Shared.Contracts;
using Zlink.Framework.AspNetCore;

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
            options.UseDiscovery().AddRegistryEndpoint(topology.RegistryRouterEndpoint);
            {
                var route = options.AddRouteMeshChannel(SampleNames.OrderWorkflowRouteChannel);
                route.EnableServer(instance.RouteEndpoint);
                route.ConfigureRouting().RoutingId = instance.RouteRid;
                route.AddRequestHandler<StartOrderWorkflowRouteHandler, StartOrderWorkflowReq, StartOrderWorkflowRes>();
                route.AddRequestHandler<ContinueOrderWorkflowRouteHandler, ContinueOrderWorkflowReq, ContinueOrderWorkflowRes>();
                route.AddRequestHandler<RebuildOrderProjectionRouteHandler, RebuildOrderProjectionReq, RebuildOrderProjectionRes>();

            }
            {
                var mesh = options.AddSpotMesh(SampleNames.OrderSpotDiscovery);
                {
                    var spot = mesh.AddNode(SampleNames.OrderSpotNode);
                    {
                        var router = spot.EnableRouter(instance.SpotRouterEndpoint);
                        router.SetRouterRoutingId(instance.SpotRid);

                    }
                    spot.EnablePubSub(instance.SpotEndpoint);
                    spot.AddSpotFactory<OrderWorkflowSpot>();

                }

            }
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
