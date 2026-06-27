using ResilienceLifecycle.Server.Provider;
using ResilienceLifecycle.Server.Provider.Handlers;

var app = ProviderHostFactory.Create(args);
await app.RunAsync();
