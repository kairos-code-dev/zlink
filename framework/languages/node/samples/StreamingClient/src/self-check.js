const assert = require('node:assert/strict');
const connector = require('../../../packages/stream-connector/dist');
const json = require('../../../packages/stream-connector-json/dist');

async function main() {
  const connection = new InMemoryStreamConnection();
  const transportFactory = new ReconnectingInMemoryTransportFactory(connection);
  const client = connector.zlinkStreamConnectorFactory.create({
    endpoint: 'tcp://sample-stream',
    dispatchMode: connector.ZlinkStreamDispatchMode.Manual,
    heartbeat: { enabled: false },
    reconnect: { enabled: true, initialDelayMs: 1, maxDelayMs: 1, backoffFactor: 1, maxAttempts: 2 },
    transportFactory
  });

  const states = [];
  const notifications = [];
  client.onConnectionStateChanged((change) => states.push(change.current));
  client.on('ServerNotice', (message) => {
    notifications.push(json.fromJson(message.payload).message);
  });

  await client.connect();
  const replyTask = client
    .request(json.toJson({ playerId: 'p1' }))
    .packetName('Join')
    .timeout(1000)
    .submit();
  await client.dispatch();
  const reply = json.fromJson(await replyTask);

  connection.pushFrame(frame({
    kind: connector.ZlinkStreamMessageKind.Send,
    name: 'ServerNotice',
    payload: json.toJson({ message: 'welcome' })
  }));
  await client.dispatch();
  await client.close();

  assert.deepEqual(reply, { accepted: true, playerId: 'p1' });
  assert.deepEqual(notifications, ['welcome']);
  assert.equal(transportFactory.connectAttempts, 2);
  assert.equal(states.includes(connector.ZlinkStreamConnectionState.Connected), true);
  assert.equal(states.includes(connector.ZlinkStreamConnectionState.Reconnecting), true);
  console.log('PASS StreamingClient');
}

class ReconnectingInMemoryTransportFactory {
  constructor(connection) {
    this.connection = connection;
    this.connectAttempts = 0;
  }

  async connect() {
    this.connectAttempts += 1;
    if (this.connectAttempts === 1) {
      throw new Error('sample reconnect attempt');
    }
    return this.connection;
  }
}

class InMemoryStreamConnection {
  constructor() {
    this.inbound = [];
  }

  async write(bytes) {
    const decoded = connector.ZlinkStreamFrameCodec.decode(bytes);
    const header = connector.ZlinkStreamHeaderCodec.decode(decoded.header);
    if (header.kind === connector.ZlinkStreamMessageKind.Request) {
      this.pushFrame(frame({
        kind: connector.ZlinkStreamMessageKind.Response,
        requestSeq: header.requestSeq,
        name: header.name,
        payload: json.toJson({ accepted: true, playerId: 'p1' })
      }));
    }
  }

  async read() {
    return this.inbound.shift();
  }

  pushFrame(bytes) {
    this.inbound.push(bytes);
  }

  async close() {}
}

function frame({ kind, name, payload, requestSeq }) {
  return connector.ZlinkStreamFrameCodec.encode(
    connector.ZlinkStreamHeaderCodec.encode({
      kind,
      codec: payload.codec,
      flags: requestSeq === undefined
        ? connector.ZlinkStreamHeaderFlags.None
        : connector.ZlinkStreamHeaderFlags.HasRequestSeq,
      requestSeq,
      name,
      metadata: connector.ZlinkStreamMetadataMap.empty
    }),
    payload.payload
  );
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
