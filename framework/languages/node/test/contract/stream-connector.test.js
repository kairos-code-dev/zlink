const assert = require('node:assert/strict');
const childProcess = require('node:child_process');
const crypto = require('node:crypto');
const fs = require('node:fs');
const net = require('node:net');
const os = require('node:os');
const path = require('node:path');
const tls = require('node:tls');
const test = require('node:test');

const connector = require('../../packages/stream-connector/dist');

test('stream connector exposes dotnet-shaped enums factory and default options', () => {
  const instance = connector.zlinkStreamConnectorFactory.create({
    endpoint: 'ws://127.0.0.1:19000',
    transportFactory: new MemoryTransportFactory()
  });

  assert.equal(instance.state, connector.ZlinkStreamConnectionState.Created);
  assert.equal(instance.options.transport, connector.ZlinkStreamTransport.WebSocket);
  assert.equal(instance.options.connectTimeoutMs, 5000);
  assert.equal(instance.options.requestTimeoutMs, 30000);
  assert.equal(instance.options.heartbeat.enabled, true);
  assert.equal(instance.options.reconnect.maxAttempts, 3);
});

test('stream header and frame codec follow dotnet binary layout', () => {
  const metadata = connector.ZlinkStreamMetadataMap.empty.with('trace', 'abc');
  const header = {
    kind: connector.ZlinkStreamMessageKind.Request,
    codec: connector.ZlinkStreamCodec.Json,
    flags: connector.ZlinkStreamHeaderFlags.HasRequestSeq,
    requestSeq: 7n,
    name: 'Join',
    metadata
  };

  const encodedHeader = connector.ZlinkStreamHeaderCodec.encode(header);
  assert.deepEqual([...encodedHeader.slice(0, 3)], [2, 1, 3]);
  assert.equal(encodedHeader[11], 4);

  const decodedHeader = connector.ZlinkStreamHeaderCodec.decode(encodedHeader);
  assert.equal(decodedHeader.kind, connector.ZlinkStreamMessageKind.Request);
  assert.equal(decodedHeader.codec, connector.ZlinkStreamCodec.Json);
  assert.equal(decodedHeader.requestSeq, 7n);
  assert.equal(decodedHeader.name, 'Join');
  assert.equal(decodedHeader.metadata.get('trace'), 'abc');

  const frame = connector.ZlinkStreamFrameCodec.encode(encodedHeader, new Uint8Array([1, 2, 3]));
  assert.equal((frame[0] << 8) | frame[1], encodedHeader.length);
  assert.equal(frame[5], 3);
  const decodedFrame = connector.ZlinkStreamFrameCodec.decode(frame);
  assert.deepEqual([...decodedFrame.payload], [1, 2, 3]);
});

test('stream connector send builder writes a dotnet-compatible send frame once', async () => {
  const transportFactory = new MemoryTransportFactory();
  const instance = connector.zlinkStreamConnectorFactory.create({
    endpoint: 'tcp://127.0.0.1:19000',
    transportFactory
  });

  await instance.connect();
  await instance
    .send({
      codec: connector.ZlinkStreamCodec.Json,
      payload: new TextEncoder().encode('{"ok":true}')
    })
    .packetName('Ready')
    .metadata('trace', 'send-1')
    .submit();

  assert.equal(transportFactory.connection.frames.length, 1);
  const frame = connector.ZlinkStreamFrameCodec.decode(transportFactory.connection.frames[0]);
  const header = connector.ZlinkStreamHeaderCodec.decode(frame.header);
  assert.equal(header.kind, connector.ZlinkStreamMessageKind.Send);
  assert.equal(header.name, 'Ready');
  assert.equal(header.metadata.get('trace'), 'send-1');

  assert.throws(
    () => instance.send({ codec: connector.ZlinkStreamCodec.Raw, payload: new Uint8Array() }).packetName('$zlink.bad'),
    /reserved zlink prefix/
  );
});

test('stream connector disconnected send fails before transport write', async () => {
  const transportFactory = new MemoryTransportFactory();
  const instance = connector.zlinkStreamConnectorFactory.create({
    endpoint: 'tcp://127.0.0.1:19000',
    transportFactory
  });

  await assert.rejects(
    () => instance.send({
      codec: connector.ZlinkStreamCodec.Raw,
      payload: new TextEncoder().encode('b')
    }).packetName('h').submit(),
    (error) => error.error?.code === connector.ZlinkStreamErrorCode.Disconnected
  );
  assert.equal(transportFactory.connection.frames.length, 0);
});

test('stream connector send and request enforce payload limit before transport write', async () => {
  const sendTransportFactory = new MemoryTransportFactory();
  const sendInstance = connector.zlinkStreamConnectorFactory.create({
    endpoint: 'tcp://127.0.0.1:19000',
    transportFactory: sendTransportFactory,
    maxSendPayloadSize: 1
  });
  await sendInstance.connect();

  await assert.rejects(
    () => sendInstance.send({
      codec: connector.ZlinkStreamCodec.Raw,
      payload: new TextEncoder().encode('bb')
    }).packetName('h').submit(),
    (error) => error.error?.code === connector.ZlinkStreamErrorCode.FrameTooLarge
  );
  assert.equal(sendTransportFactory.connection.frames.length, 0);

  const requestTransportFactory = new MemoryTransportFactory();
  const requestInstance = connector.zlinkStreamConnectorFactory.create({
    endpoint: 'tcp://127.0.0.1:19000',
    transportFactory: requestTransportFactory,
    maxSendPayloadSize: 1
  });
  await requestInstance.connect();

  await assert.rejects(
    () => requestInstance.request({
      codec: connector.ZlinkStreamCodec.Raw,
      payload: new TextEncoder().encode('bb')
    }).packetName('h').timeout(1000).submit(),
    (error) => error.error?.code === connector.ZlinkStreamErrorCode.FrameTooLarge
  );
  assert.equal(requestTransportFactory.connection.frames.length, 0);
  assert.equal(requestInstance.pendingDispatchCount, 0);
});

test('stream connector compression requires configured LZ4 codec before transport write', async () => {
  const transportFactory = new MemoryTransportFactory();
  const instance = connector.zlinkStreamConnectorFactory.create({
    endpoint: 'tcp://127.0.0.1:19000',
    transportFactory
  });
  await instance.connect();

  await assert.rejects(
    () => instance.send({
      codec: connector.ZlinkStreamCodec.Raw,
      payload: new TextEncoder().encode('body')
    }).packetName('Compressed').compress().submit(),
    (error) => error.error?.code === connector.ZlinkStreamErrorCode.CompressionFailed
  );
  assert.equal(transportFactory.connection.frames.length, 0);
});

test('stream connector compressed sends write dotnet LZ4-pickled payloads', async () => {
  const transportFactory = new MemoryTransportFactory();
  const instance = connector.zlinkStreamConnectorFactory.create({
    endpoint: 'tcp://127.0.0.1:19000',
    transportFactory,
    compression: connector.ZlinkStreamCompression.Lz4
  });
  const body = new TextEncoder().encode('compressed-body');
  await instance.connect();

  await instance.send({
    codec: connector.ZlinkStreamCodec.Raw,
    payload: body
  }).packetName('Compressed').compress().submit();

  const frame = connector.ZlinkStreamFrameCodec.decode(transportFactory.connection.frames[0]);
  const header = connector.ZlinkStreamHeaderCodec.decode(frame.header);
  assert.equal((header.flags & connector.ZlinkStreamHeaderFlags.PayloadCompressed) !== 0, true);
  assert.deepEqual([...unpickleLz4(frame.payload)], [...body]);
});

test('stream connector request resolves when dispatch reads matching response frame', async () => {
  const transportFactory = new MemoryTransportFactory();
  const instance = connector.zlinkStreamConnectorFactory.create({
    endpoint: 'tcp://127.0.0.1:19000',
    transportFactory
  });

  await instance.connect();
  const pending = instance
    .request({
      codec: connector.ZlinkStreamCodec.Json,
      payload: new TextEncoder().encode('{"join":true}')
    })
    .packetName('Join')
    .timeout(1000)
    .submit();

  const requestFrame = connector.ZlinkStreamFrameCodec.decode(transportFactory.connection.frames[0]);
  const requestHeader = connector.ZlinkStreamHeaderCodec.decode(requestFrame.header);
  assert.equal(instance.pendingDispatchCount, 1);
  assert.equal(requestHeader.kind, connector.ZlinkStreamMessageKind.Request);
  assert.equal(requestHeader.requestSeq, 1n);

  transportFactory.connection.pushFrame(
    connector.ZlinkStreamFrameCodec.encode(
      connector.ZlinkStreamHeaderCodec.encode({
        kind: connector.ZlinkStreamMessageKind.Response,
        codec: connector.ZlinkStreamCodec.Json,
        flags: connector.ZlinkStreamHeaderFlags.HasRequestSeq,
        requestSeq: requestHeader.requestSeq,
        name: 'JoinReply',
        metadata: connector.ZlinkStreamMetadataMap.empty
      }),
      new TextEncoder().encode('{"accepted":true}')
    )
  );

  await instance.dispatch();
  const reply = await pending;
  assert.equal(reply.codec, connector.ZlinkStreamCodec.Json);
  assert.equal(new TextDecoder().decode(reply.payload), '{"accepted":true}');
  assert.equal(instance.pendingDispatchCount, 0);
});

test('stream connector request resolves compressed response payloads', async () => {
  const transportFactory = new MemoryTransportFactory();
  const instance = connector.zlinkStreamConnectorFactory.create({
    endpoint: 'tcp://127.0.0.1:19000',
    transportFactory,
    compression: connector.ZlinkStreamCompression.Lz4
  });

  await instance.connect();
  const pending = instance
    .request({
      codec: connector.ZlinkStreamCodec.Raw,
      payload: new TextEncoder().encode('request')
    })
    .packetName('CompressedRequest')
    .timeout(1000)
    .submit();

  const requestFrame = connector.ZlinkStreamFrameCodec.decode(transportFactory.connection.frames[0]);
  const requestHeader = connector.ZlinkStreamHeaderCodec.decode(requestFrame.header);
  const compressedPayload = Uint8Array.from(Buffer.from('40551F41010047504141414141', 'hex'));
  transportFactory.connection.pushFrame(
    connector.ZlinkStreamFrameCodec.encode(
      connector.ZlinkStreamHeaderCodec.encode({
        kind: connector.ZlinkStreamMessageKind.Response,
        codec: connector.ZlinkStreamCodec.Raw,
        flags: connector.ZlinkStreamHeaderFlags.HasRequestSeq | connector.ZlinkStreamHeaderFlags.PayloadCompressed,
        requestSeq: requestHeader.requestSeq,
        name: 'CompressedReply',
        metadata: connector.ZlinkStreamMetadataMap.empty
      }),
      compressedPayload
    )
  );

  await instance.dispatch();
  const reply = await pending;
  assert.equal(new TextDecoder().decode(reply.payload), 'A'.repeat(96));
});

test('stream connector dispatch invokes typed handlers for send frames', async () => {
  const transportFactory = new MemoryTransportFactory();
  const instance = connector.zlinkStreamConnectorFactory.create({
    endpoint: 'tcp://127.0.0.1:19000',
    transportFactory
  });
  const received = [];
  instance.on('Notice', (message) => {
    received.push({
      name: message.name,
      trace: message.metadata.get('trace'),
      payload: new TextDecoder().decode(message.payload.payload)
    });
  });

  await instance.connect();
  transportFactory.connection.pushFrame(
    connector.ZlinkStreamFrameCodec.encode(
      connector.ZlinkStreamHeaderCodec.encode({
        kind: connector.ZlinkStreamMessageKind.Send,
        codec: connector.ZlinkStreamCodec.Json,
        flags: connector.ZlinkStreamHeaderFlags.HasMetadata,
        name: 'Notice',
        metadata: connector.ZlinkStreamMetadataMap.empty.with('trace', 'handler-1')
      }),
      new TextEncoder().encode('{"notice":1}')
    )
  );

  await instance.dispatch();
  assert.deepEqual(received, [
    {
      name: 'Notice',
      trace: 'handler-1',
      payload: '{"notice":1}'
    }
  ]);
});

test('stream connector dispatch decompresses send frames for handlers', async () => {
  const transportFactory = new MemoryTransportFactory();
  const instance = connector.zlinkStreamConnectorFactory.create({
    endpoint: 'tcp://127.0.0.1:19000',
    transportFactory,
    compression: connector.ZlinkStreamCompression.Lz4
  });
  const received = [];
  instance.on('CompressedNotice', (message) => {
    received.push(new TextDecoder().decode(message.payload.payload));
  });

  await instance.connect();
  transportFactory.connection.pushFrame(
    connector.ZlinkStreamFrameCodec.encode(
      connector.ZlinkStreamHeaderCodec.encode({
        kind: connector.ZlinkStreamMessageKind.Send,
        codec: connector.ZlinkStreamCodec.Raw,
        flags: connector.ZlinkStreamHeaderFlags.PayloadCompressed,
        name: 'CompressedNotice',
        metadata: connector.ZlinkStreamMetadataMap.empty
      }),
      Uint8Array.from(Buffer.from('40551F41010047504141414141', 'hex'))
    )
  );

  await instance.dispatch();
  assert.deepEqual(received, ['A'.repeat(96)]);
});

test('stream connector publishes decode error for compressed frames without codec', async () => {
  const transportFactory = new MemoryTransportFactory();
  const instance = connector.zlinkStreamConnectorFactory.create({
    endpoint: 'tcp://127.0.0.1:19000',
    transportFactory
  });
  const errors = [];
  instance.onErrorReceived((error) => {
    errors.push(error);
  });
  instance.on('CompressedNotice', () => {});

  await instance.connect();
  transportFactory.connection.pushFrame(
    connector.ZlinkStreamFrameCodec.encode(
      connector.ZlinkStreamHeaderCodec.encode({
        kind: connector.ZlinkStreamMessageKind.Send,
        codec: connector.ZlinkStreamCodec.Raw,
        flags: connector.ZlinkStreamHeaderFlags.PayloadCompressed,
        name: 'CompressedNotice',
        metadata: connector.ZlinkStreamMetadataMap.empty
      }),
      Uint8Array.from(Buffer.from('40551F41010047504141414141', 'hex'))
    )
  );

  await instance.dispatch();
  assert.equal(errors.length, 1);
  assert.equal(errors[0].code, connector.ZlinkStreamErrorCode.FrameDecodeFailed);
});

test('stream connector dispatch publishes decode errors for invalid header frames', async () => {
  const transportFactory = new MemoryTransportFactory();
  const instance = connector.zlinkStreamConnectorFactory.create({
    endpoint: 'tcp://127.0.0.1:19000',
    transportFactory
  });
  const errors = [];
  instance.onErrorReceived((error) => {
    errors.push(error);
  });

  await instance.connect();
  transportFactory.connection.pushFrame(
    connector.ZlinkStreamFrameCodec.encode(
      new TextEncoder().encode('invalid-header'),
      new TextEncoder().encode('payload')
    )
  );

  await instance.dispatch();

  assert.equal(errors.length, 1);
  assert.equal(errors[0].code, connector.ZlinkStreamErrorCode.FrameDecodeFailed);
});

test('stream connector dispatch publishes uncorrelated remote error packets', async () => {
  const transportFactory = new MemoryTransportFactory();
  const instance = connector.zlinkStreamConnectorFactory.create({
    endpoint: 'tcp://127.0.0.1:19000',
    transportFactory
  });
  const errors = [];
  instance.onErrorReceived((error) => {
    errors.push(error);
  });

  await instance.connect();
  transportFactory.connection.pushFrame(
    connector.ZlinkStreamFrameCodec.encode(
      connector.ZlinkStreamHeaderCodec.encode({
        kind: connector.ZlinkStreamMessageKind.Error,
        codec: connector.ZlinkStreamCodec.Json,
        flags: connector.ZlinkStreamHeaderFlags.None,
        name: 'RemoteError',
        metadata: connector.ZlinkStreamMetadataMap.empty
      }),
      new TextEncoder().encode('remote failed')
    )
  );

  await instance.dispatch();

  assert.equal(errors.length, 1);
  assert.equal(errors[0].code, connector.ZlinkStreamErrorCode.RemoteError);
  assert.equal(errors[0].message, 'remote failed');
});

test('stream connector dispatch publishes user callback failures without throwing', async () => {
  const transportFactory = new MemoryTransportFactory();
  const instance = connector.zlinkStreamConnectorFactory.create({
    endpoint: 'tcp://127.0.0.1:19000',
    transportFactory
  });
  const errors = [];
  instance.onErrorReceived((error) => {
    errors.push(error);
  });
  instance.on('Notice', () => {
    throw new Error('handler failed');
  });

  await instance.connect();
  transportFactory.connection.pushFrame(
    connector.ZlinkStreamFrameCodec.encode(
      connector.ZlinkStreamHeaderCodec.encode({
        kind: connector.ZlinkStreamMessageKind.Send,
        codec: connector.ZlinkStreamCodec.Json,
        flags: connector.ZlinkStreamHeaderFlags.None,
        name: 'Notice',
        metadata: connector.ZlinkStreamMetadataMap.empty
      }),
      new TextEncoder().encode('{"notice":1}')
    )
  );

  await instance.dispatch();

  assert.equal(errors.length, 1);
  assert.equal(errors[0].code, connector.ZlinkStreamErrorCode.UserCallbackFailed);
});

test('stream connector rejects reserved packet names for user handlers', () => {
  const instance = connector.zlinkStreamConnectorFactory.create({
    endpoint: 'tcp://127.0.0.1:19000',
    transportFactory: new MemoryTransportFactory()
  });

  assert.throws(
    () => instance.on('$zlink.user', () => {}),
    (error) => error.error?.code === connector.ZlinkStreamErrorCode.ValidationFailed
  );
});

test('stream connector default TCP transport sends request and dispatches response frame', async () => {
  const server = net.createServer((socket) => {
    let buffer = Buffer.alloc(0);
    socket.on('data', (chunk) => {
      buffer = Buffer.concat([buffer, chunk]);
      const frame = tryReadFrame(buffer);
      if (frame === undefined) {
        return;
      }
      buffer = buffer.subarray(frame.length);
      const decoded = connector.ZlinkStreamFrameCodec.decode(frame.bytes);
      const header = connector.ZlinkStreamHeaderCodec.decode(decoded.header);
      socket.write(connector.ZlinkStreamFrameCodec.encode(
        connector.ZlinkStreamHeaderCodec.encode({
          kind: connector.ZlinkStreamMessageKind.Response,
          codec: connector.ZlinkStreamCodec.Json,
          flags: connector.ZlinkStreamHeaderFlags.HasRequestSeq,
          requestSeq: header.requestSeq,
          name: 'TcpReply',
          metadata: connector.ZlinkStreamMetadataMap.empty
        }),
        new TextEncoder().encode('{"tcp":true}')
      ));
    });
  });

  await listen(server);
  const { port } = server.address();
  const instance = connector.zlinkStreamConnectorFactory.create({
    endpoint: `tcp://127.0.0.1:${port}`
  });

  try {
    await instance.connect();
    const pending = instance.request({
      codec: connector.ZlinkStreamCodec.Json,
      payload: new TextEncoder().encode('{"tcp":1}')
    }).packetName('TcpRequest').timeout(1000).submit();

    await instance.dispatch();
    const reply = await pending;
    assert.equal(new TextDecoder().decode(reply.payload), '{"tcp":true}');
  } finally {
    await instance.close();
    await closeServer(server);
  }
});

test('stream connector immediate dispatch invokes handlers without manual dispatch', async () => {
  const server = net.createServer((socket) => {
    socket.write(connector.ZlinkStreamFrameCodec.encode(
      connector.ZlinkStreamHeaderCodec.encode({
        kind: connector.ZlinkStreamMessageKind.Send,
        codec: connector.ZlinkStreamCodec.Raw,
        flags: connector.ZlinkStreamHeaderFlags.None,
        name: 'Immediate',
        metadata: connector.ZlinkStreamMetadataMap.empty
      }),
      new Uint8Array()
    ));
  });

  await listen(server);
  const { port } = server.address();
  const instance = connector.zlinkStreamConnectorFactory.create({
    endpoint: `tcp://127.0.0.1:${port}`,
    dispatchMode: connector.ZlinkStreamDispatchMode.Immediate,
    heartbeat: { enabled: false }
  });
  const received = new Promise((resolve) => {
    instance.on('Immediate', () => {
      resolve();
    });
  });

  try {
    await instance.connect();
    await withTimeout(received, 1000, 'immediate dispatch');
    assert.equal(instance.pendingDispatchCount, 0);
  } finally {
    await instance.close();
    await closeServer(server);
  }
});

test('stream connector TLS transport sends frame with skipped certificate validation', async () => {
  const credentials = createTestTlsCredentials();
  let resolveReceived;
  let rejectReceived;
  const received = new Promise((resolve, reject) => {
    resolveReceived = resolve;
    rejectReceived = reject;
  });
  const server = tls.createServer(credentials, (socket) => {
    let buffer = Buffer.alloc(0);
    socket.on('data', (chunk) => {
      buffer = Buffer.concat([buffer, chunk]);
      const frame = tryReadFrame(buffer);
      if (frame === undefined) {
        return;
      }
      const decoded = connector.ZlinkStreamFrameCodec.decode(frame.bytes);
      const header = connector.ZlinkStreamHeaderCodec.decode(decoded.header);
      resolveReceived({
        name: header.name,
        codec: header.codec,
        payload: new TextDecoder().decode(decoded.payload)
      });
    });
    socket.on('error', rejectReceived);
  });

  await listen(server);
  const { port } = server.address();
  const instance = connector.zlinkStreamConnectorFactory.create({
    endpoint: `tls://127.0.0.1:${port}`,
    heartbeat: { enabled: false },
    skipServerCertificateValidation: true
  });

  try {
    await instance.connect();
    await instance.send({
      codec: connector.ZlinkStreamCodec.Raw,
      payload: new TextEncoder().encode('tls-body')
    }).packetName('TlsPacket').submit();

    assert.deepEqual(await received, {
      name: 'TlsPacket',
      codec: connector.ZlinkStreamCodec.Raw,
      payload: 'tls-body'
    });
  } finally {
    await instance.close();
    await closeServer(server);
  }
});

test('stream connector default WebSocket transport sends request and dispatches binary response frame', async () => {
  const server = net.createServer((socket) => handleWebSocketEcho(socket, 'WsReply', '{"ws":true}'));

  await listen(server);
  const { port } = server.address();
  const instance = connector.zlinkStreamConnectorFactory.create({
    endpoint: `ws://127.0.0.1:${port}/zlink`
  });

  try {
    await instance.connect();
    const pending = instance.request({
      codec: connector.ZlinkStreamCodec.Json,
      payload: new TextEncoder().encode('{"ws":1}')
    }).packetName('WsRequest').timeout(1000).submit();

    await instance.dispatch();
    const reply = await pending;
    assert.equal(new TextDecoder().decode(reply.payload), '{"ws":true}');
  } finally {
    await instance.close();
    await closeServer(server);
  }
});

test('stream connector secure WebSocket transport sends request and dispatches binary response frame', async () => {
  const credentials = createTestTlsCredentials();
  const server = tls.createServer(credentials, (socket) => handleWebSocketEcho(socket, 'WssReply', '{"wss":true}'));

  await listen(server);
  const { port } = server.address();
  const instance = connector.zlinkStreamConnectorFactory.create({
    endpoint: `wss://127.0.0.1:${port}/zlink`,
    skipServerCertificateValidation: true
  });

  try {
    await instance.connect();
    const pending = instance.request({
      codec: connector.ZlinkStreamCodec.Json,
      payload: new TextEncoder().encode('{"wss":1}')
    }).packetName('WssRequest').timeout(1000).submit();

    await instance.dispatch();
    const reply = await pending;
    assert.equal(new TextDecoder().decode(reply.payload), '{"wss":true}');
  } finally {
    await instance.close();
    await closeServer(server);
  }
});

test('stream connector dispatch replies to heartbeat ping control frames with pong', async () => {
  const transportFactory = new MemoryTransportFactory();
  const instance = connector.zlinkStreamConnectorFactory.create({
    endpoint: 'tcp://127.0.0.1:19000',
    transportFactory
  });

  await instance.connect();
  transportFactory.connection.pushFrame(
    connector.ZlinkStreamFrameCodec.encode(
      connector.ZlinkStreamHeaderCodec.encode({
        kind: connector.ZlinkStreamMessageKind.Control,
        codec: connector.ZlinkStreamCodec.Raw,
        flags: connector.ZlinkStreamHeaderFlags.None,
        name: '$zlink.heartbeat.ping',
        metadata: connector.ZlinkStreamMetadataMap.empty
      }),
      new Uint8Array()
    )
  );

  await instance.dispatch();
  const frame = connector.ZlinkStreamFrameCodec.decode(transportFactory.connection.frames[0]);
  const header = connector.ZlinkStreamHeaderCodec.decode(frame.header);
  assert.equal(header.kind, connector.ZlinkStreamMessageKind.Control);
  assert.equal(header.name, '$zlink.heartbeat.pong');
});

test('stream connector validates endpoint and lifecycle options like dotnet transport factory', () => {
  assert.throws(
    () => connector.zlinkStreamConnectorFactory.create({ endpoint: 'http://127.0.0.1:1' }),
    /Endpoint scheme is not supported/
  );
  assert.throws(
    () => connector.zlinkStreamConnectorFactory.create({ endpoint: 'tcp://127.0.0.1:1', transport: connector.ZlinkStreamTransport.Tls }),
    /conflicts with endpoint scheme/
  );
  assert.throws(
    () => connector.zlinkStreamConnectorFactory.create({ endpoint: 'tcp://127.0.0.1:1', heartbeat: { intervalMs: 5, timeoutMs: 5 } }),
    /Heartbeat timeout must be greater/
  );
  assert.throws(
    () => connector.zlinkStreamConnectorFactory.create({ endpoint: 'tcp://127.0.0.1:1', reconnect: { backoffFactor: 0.5 } }),
    /BackoffFactor/
  );
});

test('stream connector heartbeat loop sends ping control frames after connect', async () => {
  const transportFactory = new MemoryTransportFactory();
  const instance = connector.zlinkStreamConnectorFactory.create({
    endpoint: 'tcp://127.0.0.1:19000',
    transportFactory,
    heartbeat: { intervalMs: 1, timeoutMs: 1000 }
  });

  await instance.connect();
  const frame = connector.ZlinkStreamFrameCodec.decode(await transportFactory.connection.nextWrite());
  const header = connector.ZlinkStreamHeaderCodec.decode(frame.header);
  assert.equal(header.kind, connector.ZlinkStreamMessageKind.Control);
  assert.equal(header.name, '$zlink.heartbeat.ping');
  await instance.close();
});

test('stream connector connect retries through reconnect options before succeeding', async () => {
  const transportFactory = new FlakyTransportFactory(1);
  const states = [];
  const instance = connector.zlinkStreamConnectorFactory.create({
    endpoint: 'tcp://127.0.0.1:19000',
    transportFactory,
    reconnect: { initialDelayMs: 1, maxDelayMs: 1, backoffFactor: 1, maxAttempts: 2 },
    heartbeat: { enabled: false }
  });
  instance.onConnectionStateChanged((change) => {
    states.push(change.current);
  });

  await instance.connect();

  assert.equal(transportFactory.attempts, 2);
  assert.equal(instance.isConnected, true);
  assert.ok(states.includes(connector.ZlinkStreamConnectionState.Reconnecting));
  await instance.close();
});

class MemoryTransportFactory {
  constructor() {
    this.connection = new MemoryConnection();
  }

  async connect() {
    return this.connection;
  }
}

function tryReadFrame(buffer) {
  if (buffer.length < 6) {
    return undefined;
  }
  const headerLength = (buffer[0] << 8) | buffer[1];
  const payloadLength = buffer[2] * 0x1000000 + ((buffer[3] << 16) | (buffer[4] << 8) | buffer[5]);
  const length = 6 + headerLength + payloadLength;
  if (buffer.length < length) {
    return undefined;
  }
  return { bytes: buffer.subarray(0, length), length };
}

function createWebSocketAcceptResponse(header) {
  const keyLine = header.split('\r\n').find((line) => line.toLowerCase().startsWith('sec-websocket-key:'));
  assert.ok(keyLine, 'expected Sec-WebSocket-Key');
  const key = keyLine.slice(keyLine.indexOf(':') + 1).trim();
  const accept = crypto
    .createHash('sha1')
    .update(`${key}258EAFA5-E914-47DA-95CA-C5AB0DC85B11`)
    .digest('base64');
  return [
    'HTTP/1.1 101 Switching Protocols',
    'Upgrade: websocket',
    'Connection: Upgrade',
    `Sec-WebSocket-Accept: ${accept}`,
    '',
    ''
  ].join('\r\n');
}

function tryReadWebSocketFrame(buffer) {
  if (buffer.length < 2) {
    return undefined;
  }
  const masked = (buffer[1] & 0x80) !== 0;
  let length = buffer[1] & 0x7f;
  let offset = 2;
  if (length === 126) {
    if (buffer.length < 4) {
      return undefined;
    }
    length = buffer.readUInt16BE(2);
    offset = 4;
  } else if (length === 127) {
    if (buffer.length < 10) {
      return undefined;
    }
    assert.equal(buffer.readUInt32BE(2), 0);
    length = buffer.readUInt32BE(6);
    offset = 10;
  }
  const maskLength = masked ? 4 : 0;
  const frameLength = offset + maskLength + length;
  if (buffer.length < frameLength) {
    return undefined;
  }
  const mask = masked ? buffer.subarray(offset, offset + 4) : undefined;
  const payload = Buffer.from(buffer.subarray(offset + maskLength, frameLength));
  if (mask !== undefined) {
    for (let index = 0; index < payload.length; index += 1) {
      payload[index] ^= mask[index % 4];
    }
  }
  return { opcode: buffer[0] & 0x0f, payload, length: frameLength };
}

function createWebSocketBinaryFrame(payload) {
  const length = payload.length;
  if (length < 126) {
    return Buffer.concat([Buffer.from([0x82, length]), Buffer.from(payload)]);
  }
  if (length <= 0xffff) {
    const header = Buffer.alloc(4);
    header[0] = 0x82;
    header[1] = 126;
    header.writeUInt16BE(length, 2);
    return Buffer.concat([header, Buffer.from(payload)]);
  }
  const header = Buffer.alloc(10);
  header[0] = 0x82;
  header[1] = 127;
  header.writeUInt32BE(0, 2);
  header.writeUInt32BE(length, 6);
  return Buffer.concat([header, Buffer.from(payload)]);
}

function listen(server) {
  return new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', () => {
      server.off('error', reject);
      resolve();
    });
  });
}

function closeServer(server) {
  return new Promise((resolve) => server.close(resolve));
}

function withTimeout(promise, timeoutMs, label) {
  let timeout;
  return Promise.race([
    promise.finally(() => clearTimeout(timeout)),
    new Promise((_, reject) => {
      timeout = setTimeout(() => reject(new Error(`${label} timed out`)), timeoutMs);
    })
  ]);
}

function unpickleLz4(payload) {
  if (payload.length === 0) {
    return new Uint8Array();
  }
  assert.equal(payload[0], 0);
  return payload.slice(1);
}

function handleWebSocketEcho(socket, replyName, replyBody) {
  let buffer = Buffer.alloc(0);
  let accepted = false;
  socket.on('data', (chunk) => {
    buffer = Buffer.concat([buffer, chunk]);
    if (!accepted) {
      const headerEnd = buffer.indexOf('\r\n\r\n');
      if (headerEnd < 0) {
        return;
      }
      const header = buffer.subarray(0, headerEnd).toString('utf8');
      buffer = buffer.subarray(headerEnd + 4);
      socket.write(createWebSocketAcceptResponse(header));
      accepted = true;
    }

    while (true) {
      const websocketFrame = tryReadWebSocketFrame(buffer);
      if (websocketFrame === undefined) {
        return;
      }
      buffer = buffer.subarray(websocketFrame.length);
      if (websocketFrame.opcode !== 0x2) {
        continue;
      }
      const decoded = connector.ZlinkStreamFrameCodec.decode(websocketFrame.payload);
      const header = connector.ZlinkStreamHeaderCodec.decode(decoded.header);
      if (header.kind !== connector.ZlinkStreamMessageKind.Request) {
        continue;
      }
      socket.write(createWebSocketBinaryFrame(connector.ZlinkStreamFrameCodec.encode(
        connector.ZlinkStreamHeaderCodec.encode({
          kind: connector.ZlinkStreamMessageKind.Response,
          codec: connector.ZlinkStreamCodec.Json,
          flags: connector.ZlinkStreamHeaderFlags.HasRequestSeq,
          requestSeq: header.requestSeq,
          name: replyName,
          metadata: connector.ZlinkStreamMetadataMap.empty
        }),
        new TextEncoder().encode(replyBody)
      )));
    }
  });
}

class MemoryConnection {
  constructor() {
    this.frames = [];
    this.inbound = [];
    this.closed = false;
    this.writeWaiters = [];
  }

  async write(frame) {
    this.frames.push(frame);
    const waiter = this.writeWaiters.shift();
    if (waiter !== undefined) {
      waiter(frame);
    }
  }

  async read() {
    return this.inbound.shift();
  }

  async nextWrite() {
    const frame = this.frames.shift();
    if (frame !== undefined) {
      return frame;
    }
    return await new Promise((resolve) => this.writeWaiters.push(resolve));
  }

  pushFrame(frame) {
    this.inbound.push(frame);
  }

  async close() {
    this.closed = true;
  }
}

class FlakyTransportFactory {
  constructor(failures) {
    this.failures = failures;
    this.attempts = 0;
    this.connection = new MemoryConnection();
  }

  async connect() {
    this.attempts += 1;
    if (this.attempts <= this.failures) {
      throw new Error('connect failed');
    }
    return this.connection;
  }
}

function createTestTlsCredentials() {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'zlink-node-tls-'));
  const keyPath = path.join(directory, 'key.pem');
  const certPath = path.join(directory, 'cert.pem');
  try {
    childProcess.execFileSync('openssl', [
      'req',
      '-x509',
      '-newkey',
      'rsa:2048',
      '-nodes',
      '-keyout',
      keyPath,
      '-out',
      certPath,
      '-days',
      '3650',
      '-subj',
      '/CN=localhost'
    ], { stdio: 'ignore' });
    return {
      key: fs.readFileSync(keyPath),
      cert: fs.readFileSync(certPath)
    };
  } finally {
    fs.rmSync(directory, { recursive: true, force: true });
  }
}
