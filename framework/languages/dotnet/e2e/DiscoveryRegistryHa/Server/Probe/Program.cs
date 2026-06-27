using DiscoveryRegistryHa.Server.Probe;

var app = ProbeHostFactory.Create(args);
await app.RunAsync();
