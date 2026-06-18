/* SPDX-License-Identifier: MPL-2.0 */

using Zlink.HttpClient.Runtime;

namespace Zlink.HttpClient;

/// <summary>
/// ZLink-style fluent HTTP client. Wraps <c>System.Net.Http.HttpClient</c> behind a builder so
/// transport types never leak into application code. A general HTTP client; the typed-JSON path
/// (<c>Body(dto)</c> / <c>SubmitAsync&lt;T&gt;()</c> / <c>Fetch&lt;T&gt;()</c>) is a convenience
/// layer on top. Mirrors the C++ <c>zlink::http_client::client_t</c>.
/// </summary>
public sealed class ZLinkHttpClient : IDisposable
{
    private readonly HttpClientRuntime _runtime;

    internal ZLinkHttpClient(HttpClientRuntime runtime) => _runtime = runtime;

    internal HttpClientRuntime Runtime => _runtime;

    /// <summary>Starts a new client builder.</summary>
    public static ZLinkHttpClientBuilder Create() => new();

    /// <summary>Starts a new client builder with a base URL.</summary>
    public static ZLinkHttpClientBuilder Create(string baseUrl) => new ZLinkHttpClientBuilder().BaseUrl(baseUrl);

    public ZLinkHttpRequestBuilder Get(string path) => new(this, ZLinkHttpMethod.Get, path);

    public ZLinkHttpRequestBuilder Post(string path) => new(this, ZLinkHttpMethod.Post, path);

    public ZLinkHttpRequestBuilder Put(string path) => new(this, ZLinkHttpMethod.Put, path);

    public ZLinkHttpRequestBuilder Delete(string path) => new(this, ZLinkHttpMethod.Delete, path);

    public ZLinkHttpRequestBuilder Patch(string path) => new(this, ZLinkHttpMethod.Patch, path);

    public ZLinkHttpRequestBuilder Head(string path) => new(this, ZLinkHttpMethod.Head, path);

    public ZLinkHttpRequestBuilder Options(string path) => new(this, ZLinkHttpMethod.Options, path);

    public void Dispose() => _runtime.Dispose();
}
