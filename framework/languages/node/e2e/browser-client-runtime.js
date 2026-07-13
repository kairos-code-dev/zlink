"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.BrowserE2eHttpClientFactory = void 0;
exports.browserE2eArgs = browserE2eArgs;
exports.browserE2eFetch = browserE2eFetch;
exports.runBrowserE2e = runBrowserE2e;
function browserE2eArgs() {
    return window.__zlinkE2eArgs ?? [];
}
async function runBrowserE2e(name, scenario) {
    window.__zlinkE2eResult = { name, status: 'running' };
    try {
        await scenario();
        window.__zlinkE2eResult = { name, status: 'passed' };
    }
    catch (error) {
        const message = error instanceof Error ? `${error.name}: ${error.message}` : String(error);
        window.__zlinkE2eResult = { name, status: 'failed', error: message };
        console.error(error);
    }
}
async function browserE2eFetch(input, init) {
    const target = new URL(input, window.location.origin);
    if (target.origin === window.location.origin) {
        return await fetch(target, init);
    }
    return await fetch(`/proxy?url=${encodeURIComponent(target.toString())}`, init);
}
class BrowserE2eHttpClientFactory {
    static create(baseUrl) {
        return new BrowserE2eHttpClientBuilder(baseUrl);
    }
}
exports.BrowserE2eHttpClientFactory = BrowserE2eHttpClientFactory;
class BrowserE2eHttpClientBuilder {
    baseUrl;
    timeoutMs = 30_000;
    constructor(baseUrl) {
        this.baseUrl = baseUrl;
    }
    timeout(timeoutMs) {
        this.timeoutMs = timeoutMs;
        return this;
    }
    build() {
        return new DefaultBrowserE2eHttpClient(this.baseUrl, this.timeoutMs);
    }
}
class DefaultBrowserE2eHttpClient {
    baseUrl;
    timeoutMs;
    constructor(baseUrl, timeoutMs) {
        this.baseUrl = baseUrl;
        this.timeoutMs = timeoutMs;
    }
    post(path) {
        return new DefaultBrowserE2eHttpRequest(new URL(path, `${this.baseUrl}/`).toString(), this.timeoutMs);
    }
    async close() { }
}
class DefaultBrowserE2eHttpRequest {
    url;
    timeoutMs;
    requestBody;
    constructor(url, timeoutMs) {
        this.url = url;
        this.timeoutMs = timeoutMs;
    }
    body(value) {
        this.requestBody = value;
        return this;
    }
    timeout(timeoutMs) {
        this.timeoutMs = timeoutMs;
        return this;
    }
    async fetch() {
        const response = await browserE2eFetch(this.url, {
            method: 'POST',
            headers: { 'content-type': 'application/json' },
            body: this.requestBody === undefined ? undefined : JSON.stringify(this.requestBody),
            signal: AbortSignal.timeout(this.timeoutMs)
        });
        if (!response.ok) {
            throw new Error(`HTTP ${response.status} from ${this.url}: ${await response.text()}`);
        }
        const text = await response.text();
        return (text.length === 0 ? undefined : JSON.parse(text));
    }
}
