using DeliveryDispatch.Server.Configuration;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

var topology = SampleTopology.Create();
var builder = Host.CreateApplicationBuilder(args);
builder.Services.AddZLinkRegistry(options =>
{
    options.PubEndpoint = topology.RegistryPubEndpoint;
    options.RouterEndpoint = topology.RegistryRouterEndpoint;
});

await builder.Build().RunAsync();
