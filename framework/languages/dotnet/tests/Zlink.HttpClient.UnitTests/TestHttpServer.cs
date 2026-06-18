/* SPDX-License-Identifier: MPL-2.0 */

using System.Net;
using System.Net.Sockets;

namespace Zlink.HttpClient.UnitTests;

/// <summary>
/// Minimal in-process HTTP server backed by <see cref="HttpListener"/> for contract tests.
/// Mirrors the role of the C++ <c>test_cpp_http_client</c> embedded server.
/// </summary>
internal sealed class TestHttpServer : IDisposable
{
    private readonly HttpListener _listener = new();
    private readonly Func<HttpListenerContext, Task> _handler;
    private readonly CancellationTokenSource _cts = new();

    public TestHttpServer(Func<HttpListenerContext, Task> handler)
    {
        _handler = handler;
        var port = FreePort();
        BaseUrl = $"http://127.0.0.1:{port}";
        _listener.Prefixes.Add($"{BaseUrl}/");
        _listener.Start();
        _ = AcceptLoopAsync();
    }

    public string BaseUrl { get; }

    private async Task AcceptLoopAsync()
    {
        while (!_cts.IsCancellationRequested)
        {
            HttpListenerContext context;
            try
            {
                context = await _listener.GetContextAsync().ConfigureAwait(false);
            }
            catch
            {
                return;
            }

            _ = Task.Run(async () =>
            {
                try
                {
                    await _handler(context).ConfigureAwait(false);
                }
                catch
                {
                    // ignore handler faults in tests
                }
                finally
                {
                    try
                    {
                        context.Response.Close();
                    }
                    catch
                    {
                        // already closed
                    }
                }
            });
        }
    }

    private static int FreePort()
    {
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var port = ((IPEndPoint)listener.LocalEndpoint).Port;
        listener.Stop();
        return port;
    }

    public void Dispose()
    {
        _cts.Cancel();
        try
        {
            _listener.Stop();
            _listener.Close();
        }
        catch
        {
            // already stopped
        }

        _cts.Dispose();
    }
}

/// <summary>Helpers for writing <see cref="HttpListener"/> responses in tests.</summary>
internal static class TestHttpServerExtensions
{
    public static async Task WriteAsync(this HttpListenerResponse response, int status, string body, string contentType = "application/json")
    {
        response.StatusCode = status;
        response.ContentType = contentType;
        var bytes = System.Text.Encoding.UTF8.GetBytes(body);
        response.ContentLength64 = bytes.Length;
        await response.OutputStream.WriteAsync(bytes).ConfigureAwait(false);
    }

    public static string ReadBody(this HttpListenerRequest request)
    {
        using var reader = new StreamReader(request.InputStream, request.ContentEncoding);
        return reader.ReadToEnd();
    }

    public static byte[] ReadBodyBytes(this HttpListenerRequest request)
    {
        using var output = new MemoryStream();
        request.InputStream.CopyTo(output);
        return output.ToArray();
    }

    public static async Task WriteBytesAsync(
        this HttpListenerResponse response,
        int status,
        byte[] body,
        string contentType)
    {
        response.StatusCode = status;
        response.ContentType = contentType;
        response.ContentLength64 = body.Length;
        await response.OutputStream.WriteAsync(body).ConfigureAwait(false);
    }
}
