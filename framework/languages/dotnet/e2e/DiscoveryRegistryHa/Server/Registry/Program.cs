using DiscoveryRegistryHa.Server.Registry;

var app = RegistryHostFactory.Create(args);
await app.RunAsync();