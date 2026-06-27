using RegistryMessaging.Consumer;

var app = ConsumerHostFactory.Create(args);
await app.RunAsync();
