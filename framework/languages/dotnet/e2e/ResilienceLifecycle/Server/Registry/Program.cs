using ResilienceLifecycle.Server.Registry;
using ResilienceLifecycle.Server.Registry.Configuration;
using ResilienceLifecycle.Server.Registry.Endpoints;
using ResilienceLifecycle.Server.Registry.Handlers;
using ResilienceLifecycle.Server.Registry.Infrastructure;

var app = RegistryHostFactory.Create(args);
await app.RunAsync();
