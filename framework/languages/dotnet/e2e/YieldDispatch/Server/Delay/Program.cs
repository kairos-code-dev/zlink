using YieldDispatch.Server.Delay;

var app = DelayHostFactory.Create(args);
await app.RunAsync();
