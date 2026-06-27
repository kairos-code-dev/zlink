using RuntimeMonitoring.Server.Registry;
using RuntimeMonitoring.Server.Registry.Handlers;
using RuntimeMonitoring.Server.Registry.Support;

var app = RegistryHostFactory.Create(args);
await app.RunAsync();
