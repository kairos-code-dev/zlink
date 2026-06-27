using ResilienceLifecycle.Server.Registry;

var app = RegistryHostFactory.Create(args);
await app.RunAsync();
