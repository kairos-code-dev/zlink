import * as crypto from 'node:crypto';
import * as net from 'node:net';
import * as tls from 'node:tls';
import { ZlinkStreamErrorCode } from '../../Contracts';
import { connectorError } from '../ZlinkStreamSupport';

const maxHandshakeHeaderBytes = 16 * 1024;

export async function completeWebSocketHandshake(
  socket: net.Socket | tls.TLSSocket,
  endpoint: URL,
  connectTimeoutMs: number,
  signal?: AbortSignal
): Promise<Buffer | undefined> {
  const key = crypto.randomBytes(16).toString('base64');
  const pathAndQuery = `${endpoint.pathname || '/'}${endpoint.search}`;
  const headers = [
    `GET ${pathAndQuery} HTTP/1.1`,
    `Host: ${endpoint.host}`,
    'Upgrade: websocket',
    'Connection: Upgrade',
    `Sec-WebSocket-Key: ${key}`,
    'Sec-WebSocket-Version: 13',
    '',
    ''
  ].join('\r\n');

  socket.write(headers);
  const response = await readHttpHeaders(socket, connectTimeoutMs, signal);
  const headerText = response.header.toString('utf8');
  const lines = headerText.split('\r\n');
  if (!/^HTTP\/1\.[01] 101(?:\s|$)/.test(lines[0] ?? '')) {
    throw connectorError(ZlinkStreamErrorCode.ConfigurationError, 'WebSocket handshake failed.');
  }
  const headersByName = new Map<string, string>();
  for (const line of lines.slice(1)) {
    const separator = line.indexOf(':');
    if (separator <= 0) {
      continue;
    }
    headersByName.set(line.slice(0, separator).trim().toLowerCase(), line.slice(separator + 1).trim());
  }
  const expectedAccept = crypto
    .createHash('sha1')
    .update(`${key}258EAFA5-E914-47DA-95CA-C5AB0DC85B11`)
    .digest('base64');
  if (headersByName.get('sec-websocket-accept') !== expectedAccept) {
    throw connectorError(ZlinkStreamErrorCode.ConfigurationError, 'WebSocket handshake accept header is invalid.');
  }
  return response.leftover.length > 0 ? response.leftover : undefined;
}

function readHttpHeaders(
  socket: net.Socket | tls.TLSSocket,
  connectTimeoutMs: number,
  signal?: AbortSignal
): Promise<{ header: Buffer; leftover: Buffer }> {
  return new Promise((resolve, reject) => {
    let buffer = Buffer.alloc(0);
    const timeout = setTimeout(() => {
      cleanup();
      reject(connectorError(ZlinkStreamErrorCode.ConnectTimeout, 'WebSocket handshake timed out.'));
    }, connectTimeoutMs);
    const cleanup = () => {
      clearTimeout(timeout);
      socket.off('data', onData);
      socket.off('error', onError);
      signal?.removeEventListener('abort', onAbort);
    };
    const onData = (chunk: Buffer) => {
      buffer = Buffer.concat([buffer, chunk]);
      const index = buffer.indexOf('\r\n\r\n');
      if ((index < 0 ? buffer.length : index) > maxHandshakeHeaderBytes) {
        cleanup();
        reject(connectorError(ZlinkStreamErrorCode.FrameTooLarge, 'WebSocket handshake header is too large.'));
        return;
      }
      if (index < 0) {
        return;
      }
      cleanup();
      resolve({
        header: buffer.subarray(0, index),
        leftover: buffer.subarray(index + 4)
      });
    };
    const onError = (error: Error) => {
      cleanup();
      reject(connectorError(ZlinkStreamErrorCode.ConnectTimeout, 'WebSocket handshake failed.', error));
    };
    const onAbort = () => {
      cleanup();
      reject(connectorError(ZlinkStreamErrorCode.Disconnected, 'WebSocket handshake canceled.'));
    };
    socket.on('data', onData);
    socket.once('error', onError);
    signal?.addEventListener('abort', onAbort, { once: true });
  });
}
