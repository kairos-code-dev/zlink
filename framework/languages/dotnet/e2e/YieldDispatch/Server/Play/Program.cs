using YieldDispatch.Server.Play;
using YieldDispatch.Server.Play.Handlers;
using YieldDispatch.Server.Play.Spots;

var app = PlayHostFactory.Create(args);
await app.RunAsync();
