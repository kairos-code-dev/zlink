using DiscoveryRegistryHa.Server.Embedded;

var app = EmbeddedHostFactory.Create(args);
await app.RunAsync();
