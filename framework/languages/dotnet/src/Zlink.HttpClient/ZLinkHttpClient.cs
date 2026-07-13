/* SPDX-License-Identifier: Apache-2.0 */

using Zlink.HttpClient.Runtime;

namespace Zlink.HttpClient;

/// <summary>
///     ZLink-style fluent HTTP client. Wraps <c>System.Net.Http.HttpClient</c> behind a builder so
///     transport types never leak into application code. A general HTTP client; the typed-JSON path
///     (<c>Body(dto)</c> / <c>SubmitAsync&lt;T&gt;()</c> / <c>Fetch&lt;T&gt;()</c>) is a convenience
///     layer on top. Mirrors the C++ <c>zlink::http_client::client_t</c>.
/// </summary>
public sealed class ZLinkHttpClient : IDisposable
{
    internal ZLinkHttpClient(HttpClientRuntime runtime)
    {
        Runtime = runtime;
    }

    internal HttpClientRuntime Runtime { get; }

    public void Dispose()
    {
        Runtime.Dispose();
    }

    /// <summary>Starts a new client builder.</summary>
    public static ZLinkHttpClientBuilder Create()
    {
        return new ZLinkHttpClientBuilder();
    }

    /// <summary>Starts a new client builder with a base URL.</summary>
    public static ZLinkHttpClientBuilder Create(string baseUrl)
    {
        return new ZLinkHttpClientBuilder().BaseUrl(baseUrl);
    }

    public ZLinkHttpRequestBuilder Get(string path)
    {
        return new ZLinkHttpRequestBuilder(this, ZLinkHttpMethod.Get, path);
    }

    public ZLinkHttpRequestBuilder Post(string path)
    {
        return new ZLinkHttpRequestBuilder(this, ZLinkHttpMethod.Post, path);
    }

    public ZLinkHttpRequestBuilder Put(string path)
    {
        return new ZLinkHttpRequestBuilder(this, ZLinkHttpMethod.Put, path);
    }

    public ZLinkHttpRequestBuilder Delete(string path)
    {
        return new ZLinkHttpRequestBuilder(this, ZLinkHttpMethod.Delete, path);
    }

    public ZLinkHttpRequestBuilder Patch(string path)
    {
        return new ZLinkHttpRequestBuilder(this, ZLinkHttpMethod.Patch, path);
    }

    public ZLinkHttpRequestBuilder Head(string path)
    {
        return new ZLinkHttpRequestBuilder(this, ZLinkHttpMethod.Head, path);
    }

    public ZLinkHttpRequestBuilder Options(string path)
    {
        return new ZLinkHttpRequestBuilder(this, ZLinkHttpMethod.Options, path);
    }
}