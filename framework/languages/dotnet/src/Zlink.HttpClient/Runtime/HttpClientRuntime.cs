/* SPDX-License-Identifier: MPL-2.0 */

using SystemHttpClient = System.Net.Http.HttpClient;

namespace Zlink.HttpClient.Runtime;

/// <summary>
///     Owns the underlying <see cref="System.Net.Http.HttpClient" /> and wires the request performer to
///     the retry policy. Handler/TLS construction lives in <see cref="HttpTransportFactory" /> and the
///     retry/timeout loop in <see cref="RetryPolicy" />. Mirrors the C++ <c>http_client_runtime.cpp</c>.
/// </summary>
internal sealed class HttpClientRuntime : IDisposable
{
    private readonly CookieJar _cookieJar = new();
    private readonly SocketsHttpHandler _handler;
    private readonly SystemHttpClient _httpClient;
    private readonly RequestPerformer _performer;
    private readonly RetryPolicy _retryPolicy;

    public HttpClientRuntime(HttpClientOptions options)
    {
        Options = options;
        _handler = HttpTransportFactory.CreateHandler(options);
        // disposeHandler: false keeps explicit handler ownership here, so Dispose() releases the
        // handler exactly once (the HttpClient does not also dispose it).
        _httpClient = new SystemHttpClient(_handler, false)
        {
            // Per-attempt timeout is enforced with a CancellationToken in RetryPolicy, not here.
            Timeout = Timeout.InfiniteTimeSpan
        };
        _performer = new RequestPerformer(options, _cookieJar, _httpClient);
        _retryPolicy = new RetryPolicy(options);
    }

    internal HttpClientOptions Options { get; }

    public void Dispose()
    {
        _httpClient.Dispose();
        _handler.Dispose();
    }

    /// <summary>
    ///     Executes the request, applying the retry policy. The submission API is the caller's native
    ///     <c>Task</c>; no thread is parked while the request is in flight.
    /// </summary>
    public ValueTask<RawHttpResponse> ExecuteAsync(HttpRequestSpec request, CancellationToken cancellationToken)
    {
        return _retryPolicy.ExecuteAsync(request, _performer.PerformAsync, cancellationToken);
    }
}