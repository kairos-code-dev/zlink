import http from 'node:http';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';
import { build } from 'esbuild';

const separator = process.argv.indexOf('--');
const entry = process.argv[2];
if (entry === undefined) throw new Error('Browser E2E entry path is required.');
const clientArgs = separator < 0 ? process.argv.slice(3) : process.argv.slice(separator + 1);
const nodeRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
process.env.PLAYWRIGHT_BROWSERS_PATH ??= path.join(nodeRoot, '.cache/ms-playwright');
const { chromium } = await import('playwright');

const output = await build({
  entryPoints: [path.resolve(entry)],
  bundle: true,
  write: false,
  format: 'esm',
  platform: 'browser',
  target: 'es2022'
});

const server = http.createServer(async (request, response) => {
  try {
    const url = new URL(request.url ?? '/', 'http://127.0.0.1');
    if (url.pathname === '/client.mjs') {
      response.writeHead(200, { 'content-type': 'text/javascript' });
      response.end(output.outputFiles[0].contents);
      return;
    }
    if (url.pathname === '/proxy') {
      await proxyRequest(request, response, url.searchParams.get('url'));
      return;
    }
    response.writeHead(200, { 'content-type': 'text/html' });
    response.end('<script type="module" src="/client.mjs"></script>');
  } catch (error) {
    response.writeHead(502, { 'content-type': 'text/plain' });
    response.end(error instanceof Error ? error.message : String(error));
  }
});

await new Promise((resolve, reject) => {
  server.once('error', reject);
  server.listen(0, '127.0.0.1', resolve);
});

const browser = await chromium.launch({ headless: true });
try {
  const context = await browser.newContext({
    ignoreHTTPSErrors: process.env.ZLINK_BROWSER_IGNORE_HTTPS_ERRORS === '1'
  });
  const page = await context.newPage();
  page.on('console', (message) => {
    const text = message.text();
    if (message.type() === 'error') console.error(text);
    else console.log(text);
  });
  await page.addInitScript((args) => { window.__zlinkE2eArgs = args; }, clientArgs);
  const address = server.address();
  await page.goto(`http://127.0.0.1:${address.port}`);
  const timeout = Number(process.env.ZLINK_BROWSER_E2E_TIMEOUT_MS ?? 300_000);
  await page.waitForFunction(
    () => window.__zlinkE2eResult?.status === 'passed' || window.__zlinkE2eResult?.status === 'failed',
    undefined,
    { timeout }
  );
  const result = await page.evaluate(() => window.__zlinkE2eResult);
  if (result?.status !== 'passed') throw new Error(result?.error ?? 'Browser E2E client failed.');
} finally {
  await browser.close();
  await new Promise((resolve) => server.close(resolve));
}

async function proxyRequest(request, response, targetValue) {
  if (targetValue === null) throw new Error('Proxy target is required.');
  const target = new URL(targetValue);
  if (target.protocol !== 'http:' || !['127.0.0.1', 'localhost'].includes(target.hostname)) {
    throw new Error('Browser E2E proxy permits loopback HTTP targets only.');
  }
  const chunks = [];
  for await (const chunk of request) chunks.push(chunk);
  const upstream = await fetch(target, {
    method: request.method,
    headers: copyHeaders(request.headers),
    body: ['GET', 'HEAD'].includes(request.method ?? 'GET') ? undefined : Buffer.concat(chunks)
  });
  const headers = Object.fromEntries(upstream.headers.entries());
  response.writeHead(upstream.status, headers);
  response.end(Buffer.from(await upstream.arrayBuffer()));
}

function copyHeaders(headers) {
  const result = {};
  for (const [name, value] of Object.entries(headers)) {
    if (value !== undefined && !['host', 'origin', 'content-length'].includes(name)) result[name] = value;
  }
  return result;
}
