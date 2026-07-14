using AutomaticTurnDispatch.Shared;

var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
for (var index = 0; index < args.Length; index++)
{
    if (!args[index].StartsWith("--", StringComparison.Ordinal)) continue;
    if (index + 1 >= args.Length) throw new ArgumentException($"Missing value for {args[index]}.");
    values[args[index][2..]] = args[++index];
}

var httpUrl = values.TryGetValue("http-url", out var configuredUrl)
    ? configuredUrl
    : throw new ArgumentException("--http-url is required.");
var builder = WebApplication.CreateBuilder(args);
builder.WebHost.UseUrls(httpUrl);
var app = builder.Build();
app.MapGet("/health", () => Results.Ok(new { status = "ready", role = "external-api" }));
app.MapGet("/delay", async (string requestId, string marker, int delayMs, CancellationToken cancellationToken) =>
{
    await Task.Delay(TimeSpan.FromMilliseconds(delayMs), cancellationToken);
    return Results.Ok(new ExternalDelayRes(requestId, marker));
});
await app.RunAsync();
