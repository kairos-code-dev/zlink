using DiscoveryRegistryHa.Registry;
using DiscoveryRegistryHa.Server.Registry;

var app = RegistryHostFactory.Create(args);
await app.RunAsync();
