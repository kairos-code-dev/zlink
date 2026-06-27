using DiscoveryRegistryHa.Server.Provider;
using DiscoveryRegistryHa.Server.Provider.Support;

var app = ProviderHostFactory.Create(args);
await app.RunAsync();
