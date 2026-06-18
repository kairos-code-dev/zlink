using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Server.Courier;
using DeliveryDispatch.Shared.Contracts;
using Zlink.Framework.Contracts.Codecs.Json;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

var courierId = ReadOption(args, "--courier")
    ?? Environment.GetEnvironmentVariable("DELIVERYDISPATCH_COURIER_ID")
    ?? "courier-a";
var mode = ReadOption(args, "--mode")
    ?? Environment.GetEnvironmentVariable("DELIVERYDISPATCH_COURIER_MODE")
    ?? "accept";
var topology = SampleTopology.Create();
var builder = Host.CreateApplicationBuilder(args);
builder.Services.AddSingleton(topology);
builder.Services.AddSingleton(new CourierOptions(courierId, mode));
builder.Services.AddZLinkFramework(options =>
{
    options.DefaultTimeout = SampleTimings.FrameworkTimeout;
    options.AddHandlersFromAssemblyOf(typeof(OfferDeliveryHandler));
    options.Codecs.AddJson();
    {
        var channel = options.AddClientServerChannel(SampleNames.CourierChannel(courierId));
        channel.EnableServer(topology.CourierEndpoint(courierId));
        channel.AddRequestHandler<OfferDeliveryHandler, OfferDelivery, OfferDeliveryResult>();

    }
});

await builder.Build().RunAsync();

static string? ReadOption(string[] args, string name)
{
    var index = Array.IndexOf(args, name);
    return index >= 0 && index + 1 < args.Length ? args[index + 1] : null;
}
