import * as crypto from 'node:crypto';
import * as net from 'node:net';
import * as tls from 'node:tls';
import {
  RequiredZlinkStreamConnectorOptions,
  ZlinkStreamConnection,
  ZlinkStreamErrorCode,
  ZlinkStreamTransport,
  ZlinkStreamTransportFactory
} from './contracts';
import { connectorError, readUInt16BE, readUInt32BE, throwIfAborted } from './support';

export class NodeStreamTransportFactory implements ZlinkStreamTransportFactory {
  async connect(options: RequiredZlinkStreamConnectorOptions, signal?: AbortSignal): Promise<ZlinkStreamConnection> {
    const endpoint = parseEndpoint(options.endpoint);
    if (options.transport === ZlinkStreamTransport.WebSocket || options.transport === ZlinkStreamTransport.WebSocketSecure) {
      return await connectWebSocket(endpoint, options, signal);
    }

    const socket = options.transport === ZlinkStreamTransport.Tls
      ? await connectTls(endpoint, options, signal)
      : await connectTcp(endpoint, options.connectTimeoutMs, signal);
    return new NodeDuplexStreamConnection(socket);
  }
}


export function inferTransport(endpoint: string): ZlinkStreamTransport {
  const url = parseEndpoint(endpoint);
  switch (url.protocol) {
    case 'tcp:':
      return ZlinkStreamTransport.Tcp;
    case 'tls:':
      return ZlinkStreamTransport.Tls;
    case 'ws:':
      return ZlinkStreamTransport.WebSocket;
    case 'wss:':
      return ZlinkStreamTransport.WebSocketSecure;
    default:
      throw connectorError(ZlinkStreamErrorCode.ConfigurationError, 'Endpoint scheme is not supported.');
  }
}


class NodeDuplexStreamConnection implements ZlinkStreamConnection {
  private readonly buffer = new BufferedByteQueue();
  private closed = false;
  private readWaiter: (() => void) | undefined;
  private error: Error | undefined;

  constructor(private readonly socket: net.Socket | tls.TLSSocket) {
    socket.on('data', (chunk: Buffer) => {
      this.buffer.push(chunk);
      this.wakeReader();
    });
    socket.on('close', () => {
      this.closed = true;
      this.wakeReader();
    });
    socket.on('error', (error) => {
      this.error = error;
      this.closed = true;
      this.wakeReader();
    });
  }

  async write(frame: Uint8Array, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    await new Promise<void>((resolve, reject) => {
      this.socket.write(frame, (error) => {
        if (error != null) {
          reject(connectorError(ZlinkStreamErrorCode.SendFailed, 'Send failed.', error));
          return;
        }
        resolve();
      });
    });
  }

  async read(signal?: AbortSignal): Promise<Uint8Array | undefined> {
    throwIfAborted(signal);
    while (true) {
      const frame = this.tryReadFrame();
      if (frame !== undefined) {
        return frame;
      }
      if (this.error !== undefined) {
        throw connectorError(ZlinkStreamErrorCode.Disconnected, 'Remote stream closed.', this.error);
      }
      if (this.closed) {
        return undefined;
      }
      await this.waitForData(signal);
    }
  }

  async close(): Promise<void> {
    this.closed = true;
    this.socket.destroy();
    this.wakeReader();
  }

  private tryReadFrame(): Uint8Array | undefined {
    if (this.buffer.size < 6) {
      return undefined;
    }
    const prefix = this.buffer.peek(6);
    const headerLength = readUInt16BE(prefix, 0);
    const payloadLength = readUInt32BE(prefix, 2);
    const frameLength = 6 + headerLength + payloadLength;
    if (this.buffer.size < frameLength) {
      return undefined;
    }
    return this.buffer.consume(frameLength);
  }

  private waitForData(signal: AbortSignal | undefined): Promise<void> {
    if (this.readWaiter !== undefined) {
      throw connectorError(ZlinkStreamErrorCode.ValidationFailed, 'Only one pending stream read is supported.');
    }
    return new Promise((resolve, reject) => {
      const onAbort = () => {
        this.readWaiter = undefined;
        reject(connectorError(ZlinkStreamErrorCode.Disconnected, 'Operation canceled.'));
      };
      if (signal !== undefined) {
        signal.addEventListener('abort', onAbort, { once: true });
      }
      this.readWaiter = () => {
        if (signal !== undefined) {
          signal.removeEventListener('abort', onAbort);
        }
        this.readWaiter = undefined;
        resolve();
      };
    });
  }

  private wakeReader(): void {
    this.readWaiter?.();
  }
}

class NodeWebSocketConnection implements ZlinkStreamConnection {
  private readonly buffer = new BufferedByteQueue();
  private closed = false;
  private readWaiter: (() => void) | undefined;
  private error: Error | undefined;
  private readonly messageQueue: Uint8Array[] = [];
  private currentMessageParts: Uint8Array[] = [];

  constructor(private readonly socket: net.Socket | tls.TLSSocket, initialData?: Buffer) {
    socket.on('data', (chunk: Buffer) => {
      this.buffer.push(chunk);
      this.drainFrames();
      this.wakeReader();
    });
    socket.on('close', () => {
      this.closed = true;
      this.wakeReader();
    });
    socket.on('error', (error) => {
      this.error = error;
      this.closed = true;
      this.wakeReader();
    });

    if (initialData !== undefined && initialData.length > 0) {
      this.buffer.push(initialData);
      this.drainFrames();
    }
  }

  async write(frame: Uint8Array, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    const encoded = encodeWebSocketFrame(frame, { opcode: 0x2, masked: true });
    await new Promise<void>((resolve, reject) => {
      this.socket.write(encoded, (error) => {
        if (error != null) {
          reject(connectorError(ZlinkStreamErrorCode.SendFailed, 'Send failed.', error));
          return;
        }
        resolve();
      });
    });
  }

  async read(signal?: AbortSignal): Promise<Uint8Array | undefined> {
    throwIfAborted(signal);
    while (true) {
      const message = this.messageQueue.shift();
      if (message !== undefined) {
        return message;
      }
      if (this.error !== undefined) {
        throw connectorError(ZlinkStreamErrorCode.Disconnected, 'Remote stream closed.', this.error);
      }
      if (this.closed) {
        return undefined;
      }
      await this.waitForData(signal);
    }
  }

  async close(): Promise<void> {
    if (!this.closed) {
      this.socket.write(encodeWebSocketFrame(new Uint8Array(), { opcode: 0x8, masked: true }));
    }
    this.closed = true;
    this.socket.destroy();
    this.wakeReader();
  }

  private drainFrames(): void {
    while (true) {
      const frame = this.tryReadWebSocketFrame();
      if (frame === undefined) {
        return;
      }
      this.handleFrame(frame);
    }
  }

  private handleFrame(frame: WebSocketFrame): void {
    if (frame.opcode === 0x8) {
      this.closed = true;
      return;
    }
    if (frame.opcode === 0x9) {
      this.socket.write(encodeWebSocketFrame(frame.payload, { opcode: 0xa, masked: true }));
      return;
    }
    if (frame.opcode === 0xa) {
      return;
    }
    if (frame.opcode !== 0x0 && frame.opcode !== 0x2) {
      this.error = connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'WebSocket text messages are not supported.');
      this.closed = true;
      return;
    }

    if (frame.opcode === 0x2) {
      this.currentMessageParts = [];
    }
    this.currentMessageParts.push(frame.payload);
    if (frame.fin) {
      this.messageQueue.push(concatParts(this.currentMessageParts));
      this.currentMessageParts = [];
    }
  }

  private tryReadWebSocketFrame(): WebSocketFrame | undefined {
    if (this.buffer.size < 2) {
      return undefined;
    }
    const firstTwo = this.buffer.peek(2);
    const fin = (firstTwo[0] & 0x80) !== 0;
    const opcode = firstTwo[0] & 0x0f;
    const masked = (firstTwo[1] & 0x80) !== 0;
    let payloadLength = firstTwo[1] & 0x7f;
    let headerLength = 2;
    if (payloadLength === 126) {
      if (this.buffer.size < 4) {
        return undefined;
      }
      const extended = this.buffer.peek(4);
      payloadLength = readUInt16BE(extended, 2);
      headerLength = 4;
    } else if (payloadLength === 127) {
      if (this.buffer.size < 10) {
        return undefined;
      }
      const extended = this.buffer.peek(10);
      const high = readUInt32BE(extended, 2);
      const low = readUInt32BE(extended, 6);
      if (high !== 0 || low > Number.MAX_SAFE_INTEGER) {
        this.error = connectorError(ZlinkStreamErrorCode.FrameTooLarge, 'WebSocket frame is too large.');
        this.closed = true;
        return undefined;
      }
      payloadLength = low;
      headerLength = 10;
    }

    const maskLength = masked ? 4 : 0;
    const frameLength = headerLength + maskLength + payloadLength;
    if (this.buffer.size < frameLength) {
      return undefined;
    }

    const raw = this.buffer.consume(frameLength);
    const mask = masked ? raw.subarray(headerLength, headerLength + 4) : undefined;
    const payloadStart = headerLength + maskLength;
    const payload = raw.slice(payloadStart);
    if (mask !== undefined) {
      for (let index = 0; index < payload.length; index += 1) {
        payload[index] ^= mask[index % 4];
      }
    }
    return { fin, opcode, payload };
  }

  private waitForData(signal: AbortSignal | undefined): Promise<void> {
    if (this.readWaiter !== undefined) {
      throw connectorError(ZlinkStreamErrorCode.ValidationFailed, 'Only one pending stream read is supported.');
    }
    return new Promise((resolve, reject) => {
      const onAbort = () => {
        this.readWaiter = undefined;
        reject(connectorError(ZlinkStreamErrorCode.Disconnected, 'Operation canceled.'));
      };
      if (signal !== undefined) {
        signal.addEventListener('abort', onAbort, { once: true });
      }
      this.readWaiter = () => {
        if (signal !== undefined) {
          signal.removeEventListener('abort', onAbort);
        }
        this.readWaiter = undefined;
        resolve();
      };
    });
  }

  private wakeReader(): void {
    this.readWaiter?.();
  }
}


class BufferedByteQueue {
  private readonly chunks: Buffer[] = [];
  private bufferedBytes = 0;

  get size(): number {
    return this.bufferedBytes;
  }

  push(chunk: Buffer): void {
    this.chunks.push(chunk);
    this.bufferedBytes += chunk.length;
  }

  peek(length: number): Uint8Array {
    const output = new Uint8Array(length);
    let offset = 0;
    for (const chunk of this.chunks) {
      const take = Math.min(chunk.length, length - offset);
      output.set(chunk.subarray(0, take), offset);
      offset += take;
      if (offset === length) {
        break;
      }
    }
    return output;
  }

  consume(length: number): Uint8Array {
    const output = new Uint8Array(length);
    let offset = 0;
    while (offset < length) {
      const chunk = this.chunks[0];
      const take = Math.min(chunk.length, length - offset);
      output.set(chunk.subarray(0, take), offset);
      offset += take;
      this.bufferedBytes -= take;
      if (take === chunk.length) {
        this.chunks.shift();
      } else {
        this.chunks[0] = chunk.subarray(take);
      }
    }
    return output;
  }
}

function parseEndpoint(endpoint: string): URL {
  try {
    return new URL(endpoint);
  } catch (cause) {
    throw connectorError(ZlinkStreamErrorCode.ConfigurationError, 'Endpoint is invalid.', cause);
  }
}

async function connectWebSocket(
  endpoint: URL,
  options: RequiredZlinkStreamConnectorOptions,
  signal?: AbortSignal
): Promise<ZlinkStreamConnection> {
  const secure = options.transport === ZlinkStreamTransport.WebSocketSecure;
  const socket = secure
    ? await connectSocket(endpoint, options.connectTimeoutMs, signal, (port, host) => tls.connect({
      port,
      host,
      servername: tlsServerName(host),
      rejectUnauthorized: !options.skipServerCertificateValidation
    }), 443)
    : await connectSocket(endpoint, options.connectTimeoutMs, signal, (port, host) => net.connect({ port, host, keepAlive: true }), 80);
  const leftover = await completeWebSocketHandshake(socket, endpoint, options.connectTimeoutMs, signal);
  return new NodeWebSocketConnection(socket, leftover);
}

async function connectTcp(endpoint: URL, connectTimeoutMs: number, signal?: AbortSignal): Promise<net.Socket> {
  return await connectSocket(endpoint, connectTimeoutMs, signal, (port, host) => net.connect({ port, host, keepAlive: true }));
}

async function connectTls(
  endpoint: URL,
  options: RequiredZlinkStreamConnectorOptions,
  signal?: AbortSignal
): Promise<tls.TLSSocket> {
  return await connectSocket(endpoint, options.connectTimeoutMs, signal, (port, host) => tls.connect({
    port,
    host,
    servername: tlsServerName(host),
    rejectUnauthorized: !options.skipServerCertificateValidation
  }));
}

function tlsServerName(host: string): string | undefined {
  return net.isIP(host) === 0 ? host : undefined;
}

async function connectSocket<TSocket extends net.Socket>(
  endpoint: URL,
  connectTimeoutMs: number,
  signal: AbortSignal | undefined,
  create: (port: number, host: string) => TSocket,
  defaultPort?: number
): Promise<TSocket> {
  const configuredPort = endpoint.port.length > 0 ? Number(endpoint.port) : defaultPort;
  if (!Number.isInteger(configuredPort) || configuredPort === undefined || configuredPort <= 0) {
    throw connectorError(ZlinkStreamErrorCode.ConfigurationError, 'Endpoint port is required.');
  }
  const port = configuredPort;
  const socket = create(port, endpoint.hostname);
  return await new Promise<TSocket>((resolve, reject) => {
    const timeout = setTimeout(() => {
      cleanup();
      socket.destroy();
      reject(connectorError(ZlinkStreamErrorCode.ConnectTimeout, 'Connect timed out.'));
    }, connectTimeoutMs);
    const cleanup = () => {
      clearTimeout(timeout);
      socket.off('connect', onConnect);
      socket.off('secureConnect', onConnect);
      socket.off('error', onError);
      signal?.removeEventListener('abort', onAbort);
    };
    const onConnect = () => {
      cleanup();
      resolve(socket);
    };
    const onError = (error: Error) => {
      cleanup();
      socket.destroy();
      reject(connectorError(ZlinkStreamErrorCode.ConnectTimeout, 'Connect failed.', error));
    };
    const onAbort = () => {
      cleanup();
      socket.destroy();
      reject(connectorError(ZlinkStreamErrorCode.Disconnected, 'Connect canceled.'));
    };
    socket.once('connect', onConnect);
    socket.once('secureConnect', onConnect);
    socket.once('error', onError);
    signal?.addEventListener('abort', onAbort, { once: true });
  });
}

async function completeWebSocketHandshake(
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

interface WebSocketFrame {
  readonly fin: boolean;
  readonly opcode: number;
  readonly payload: Uint8Array;
}

function encodeWebSocketFrame(payload: Uint8Array, options: { opcode: number; masked: boolean }): Buffer {
  const length = payload.length;
  const headerLength = length < 126 ? 2 : length <= 0xffff ? 4 : 10;
  const maskLength = options.masked ? 4 : 0;
  const output = Buffer.alloc(headerLength + maskLength + length);
  output[0] = 0x80 | options.opcode;
  if (length < 126) {
    output[1] = length;
  } else if (length <= 0xffff) {
    output[1] = 126;
    output.writeUInt16BE(length, 2);
  } else {
    output[1] = 127;
    output.writeUInt32BE(0, 2);
    output.writeUInt32BE(length, 6);
  }
  if (!options.masked) {
    Buffer.from(payload).copy(output, headerLength);
    return output;
  }

  output[1] |= 0x80;
  const mask = crypto.randomBytes(4);
  mask.copy(output, headerLength);
  for (let index = 0; index < length; index += 1) {
    output[headerLength + maskLength + index] = payload[index] ^ mask[index % 4];
  }
  return output;
}

function concatParts(parts: Uint8Array[]): Uint8Array {
  const length = parts.reduce((sum, part) => sum + part.length, 0);
  const output = new Uint8Array(length);
  let offset = 0;
  for (const part of parts) {
    output.set(part, offset);
    offset += part.length;
  }
  return output;
}
