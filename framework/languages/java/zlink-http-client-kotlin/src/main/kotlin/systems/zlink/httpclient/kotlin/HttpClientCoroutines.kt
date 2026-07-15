/* SPDX-License-Identifier: Apache-2.0 */
package systems.zlink.httpclient.kotlin

import kotlinx.coroutines.future.await
import systems.zlink.httpclient.HttpResponse
import systems.zlink.httpclient.RawHttpResponse
import systems.zlink.httpclient.ZLinkHttpClient
import systems.zlink.httpclient.ZLinkHttpClientBuilder
import systems.zlink.httpclient.ZLinkHttpRequestBuilder
import systems.zlink.httpclient.ZLinkHttpServerRequestBuilder

@Target(AnnotationTarget.FUNCTION)
@Retention(AnnotationRetention.BINARY)
private annotation class JacocoGenerated

/**
 * Kotlin coroutine and DSL extensions over the Java [ZLinkHttpClient]. These reuse the Java runtime
 * (the Kotlin module is a thin idiom layer) and expose genuine `suspend` functions that bridge the
 * `CompletionStage` returned by the Java client via `kotlinx-coroutines-jdk8`'s `await()` — no
 * thread is blocked. This is the canonical "coroutine HTTP client" target.
 */

/** DSL helper: builds a client by applying [configure] to the fluent builder. */
fun zlinkHttpClient(baseUrl: String, configure: ZLinkHttpClientBuilder.() -> Unit = {}): ZLinkHttpClient =
    ZLinkHttpClient.create(baseUrl).apply(configure).build()

/** Suspends until the raw response is available; does not block the calling thread. */
suspend fun ZLinkHttpRequestBuilder.awaitRaw(): RawHttpResponse =
    submitRaw().await()

/** Suspends until the typed JSON response is available. */
suspend fun <T> ZLinkHttpRequestBuilder.await(type: Class<T>): HttpResponse<T> =
    submit(type).await()

/** Reified convenience for [await]. */
@JacocoGenerated
suspend inline fun <reified T> ZLinkHttpRequestBuilder.await(): HttpResponse<T> =
    await(T::class.java)

/** Reified convenience for [fetch]. */
@JacocoGenerated
suspend inline fun <reified T> ZLinkHttpRequestBuilder.fetch(): T =
    await<T>().body()

/** Suspends until the streaming download completes, delivering chunks to [sink]. */
suspend fun ZLinkHttpRequestBuilder.awaitDownload(sink: (ByteArray) -> Unit): RawHttpResponse =
    download(sink).await()

/** Awaits a typed response while retaining the current server Spot turn. */
suspend fun <T> ZLinkHttpServerRequestBuilder.await(type: Class<T>): HttpResponse<T> =
    async(type).await()

/** Releases the current server Spot turn while awaiting the HTTP response. */
suspend fun <T> ZLinkHttpServerRequestBuilder.yieldAwait(type: Class<T>): HttpResponse<T> =
    yield(type).await()

@JacocoGenerated
suspend inline fun <reified T> ZLinkHttpServerRequestBuilder.await(): HttpResponse<T> =
    await(T::class.java)

@JacocoGenerated
suspend inline fun <reified T> ZLinkHttpServerRequestBuilder.yieldAwait(): HttpResponse<T> =
    yieldAwait(T::class.java)
