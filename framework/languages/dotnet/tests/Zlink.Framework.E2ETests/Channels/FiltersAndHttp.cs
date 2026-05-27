using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.E2ETests.Channels;
public sealed class FiltersAndHttpTests
{
    [Fact]
    public async Task Filters_Run_In_Registration_Order_Around_Handler_Dispatch()
    {
        var apiEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var serverBuilder = Host.CreateApplicationBuilder();
        serverBuilder.Services.AddSingleton<FilterOrderRecorder>();
        serverBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf<FiltersAndHttpTests>();
            options.UseFilter<OuterOrderFilter>();
            options.UseFilter<InnerOrderFilter>();
            options.AddClientServerChannel("api", channel =>
            {
                channel.EnableServer(server => server.Bind(apiEndpoint));
                channel.AddHandlerGroup("filter-order");
            });
        });
        var clientBuilder = Host.CreateApplicationBuilder();
        clientBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddClientServerChannel("api", channel =>
            {
                channel.EnableClient(client =>
                {
                    client.UseManualConnections(connections => connections.Connect(apiEndpoint));
                });
            });
        });

        using var serverHost = serverBuilder.Build();
        using var clientHost = clientBuilder.Build();

        await serverHost.StartAsync();
        await clientHost.StartAsync();

        var client = clientHost.Services.GetRequiredService<IZLinkChannelClient>();
        var recorder = serverHost.Services.GetRequiredService<FilterOrderRecorder>();

        var reply = await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
            async () => await client.RequestChannel("api", new GetFilterOrderRequest()).SubmitAsync<FilterOrderReply>(),
            static result => result.Sequence.Count == 5);

        Assert.Equal(
            ["outer:before", "inner:before", "handler", "inner:after", "outer:after"],
            reply.Sequence);
        Assert.Equal(reply.Sequence, recorder.Entries.ToArray());

        await ChannelMessagingTestSupport.StopHostsAsync(clientHost, serverHost);
    }

    [Fact]
    public async Task HttpHandler_Uses_SameServiceProvider_ToResolve_IZLinkChannelClient()
    {
        var apiEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";

        var channelBuilder = Host.CreateApplicationBuilder();
        channelBuilder.Services.AddSingleton<ProfileCommandRecorder>();
        channelBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf<FiltersAndHttpTests>();
            options.AddClientServerChannel("api", channel =>
            {
                channel.EnableServer(server => server.Bind(apiEndpoint));
                channel.AddHandlerGroup("profile");
            });
        });
        var httpBuilder = Host.CreateApplicationBuilder();
        httpBuilder.Services.AddLogging();
        httpBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddClientServerChannel("api", channel =>
            {
                channel.EnableClient(client =>
                {
                    client.UseManualConnections(connections => connections.Connect(apiEndpoint));
                });
            });
        });

        using var channelHost = channelBuilder.Build();
        using var httpHost = httpBuilder.Build();
        await channelHost.StartAsync();
        await httpHost.StartAsync();

        var handler = RequestDelegateFactory.Create(
            async (HttpContext context, [FromServices] IZLinkChannelClient client, CancellationToken cancellationToken) =>
            {
                var userId = context.Request.Query["userId"].ToString();
                var reply = await client.RequestChannel("api", new GetProfileRequest { UserId = userId })
                    .SubmitAsync<ProfileReply>(cancellationToken);
                return Results.Text(reply.Name);
            }).RequestDelegate;

        var reply = await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
            async () =>
            {
                var context = new DefaultHttpContext
                {
                    RequestServices = httpHost.Services,
                };
                context.Request.QueryString = new QueryString("?userId=http-user");
                context.Response.Body = new MemoryStream();

                await handler(context);
                context.Response.Body.Position = 0;
                using var reader = new StreamReader(context.Response.Body, leaveOpen: true);
                var content = await reader.ReadToEndAsync();
                if (context.Response.StatusCode != StatusCodes.Status200OK)
                {
                    throw new InvalidOperationException(
                        $"HTTP handler status={context.Response.StatusCode}, body='{content}'.");
                }

                return content;
            },
            static result => string.Equals(result, "user:http-user", StringComparison.Ordinal));

        Assert.Equal("user:http-user", reply);

        await ChannelMessagingTestSupport.StopHostsAsync(httpHost, channelHost);
    }
}
