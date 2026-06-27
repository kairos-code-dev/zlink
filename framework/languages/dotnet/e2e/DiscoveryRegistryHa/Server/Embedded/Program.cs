using DiscoveryRegistryHa.Server.Embedded;
using DiscoveryRegistryHa.Server.Embedded.Support;

var app = EmbeddedHostFactory.Create(args);
await app.RunAsync();
