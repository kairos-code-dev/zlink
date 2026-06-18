/* SPDX-License-Identifier: MPL-2.0 */

namespace Zlink.HttpClient;

/// <summary>
/// Raw HTTP response with status, headers, and the buffered body as a string.
/// Header lookup is case-insensitive. Mirrors the C++ <c>raw_http_response_t</c>.
/// </summary>
public sealed class RawHttpResponse
{
    public required int Status { get; init; }

    public required IReadOnlyDictionary<string, string> Headers { get; init; }

    public required string Body { get; init; }
}

/// <summary>
/// Typed HTTP response. <see cref="Body"/> is the JSON-decoded payload; <see cref="RawBody"/>
/// keeps the original response text. Mirrors the C++ <c>http_response_t&lt;T&gt;</c>.
/// </summary>
/// <typeparam name="T">The decoded body type.</typeparam>
public sealed class HttpResponse<T>
{
    public required int Status { get; init; }

    public required IReadOnlyDictionary<string, string> Headers { get; init; }

    public required T Body { get; init; }

    public required string RawBody { get; init; }
}
