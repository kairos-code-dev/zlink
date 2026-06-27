using YieldDispatch.Server.Session;
using YieldDispatch.Server.Session.Support;

var app = SessionHostFactory.Create(args);
await app.RunAsync();
