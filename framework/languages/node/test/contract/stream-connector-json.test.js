const assert = require('node:assert/strict');
const test = require('node:test');

const connector = require('../../packages/stream-connector/dist');
const json = require('../../packages/stream-connector-json/dist');

test('stream connector json codec encodes and decodes json payloads', () => {
  const payload = json.toJson({ ready: true });

  assert.equal(payload.codec, connector.ZlinkStreamCodec.Json);
  assert.deepEqual(json.fromJson(payload), { ready: true });
  assert.throws(
    () => json.fromJson({ codec: connector.ZlinkStreamCodec.Raw, payload: new Uint8Array() }),
    /not Json/
  );
});

test('stream connector json send wrapper writes json payload frame', async () => {
  const transportFactory = new MemoryTransportFactory();
  const instance = connector.zlinkStreamConnectorFactory.create({
    endpoint: 'tcp://127.0.0.1:19000',
    transportFactory
  });

  await instance.connect();
  await json.sendJson(instance, { ready: true }).packetName('Ready').metadata('trace', 'json-1').submit();

  const frame = connector.ZlinkStreamFrameCodec.decode(transportFactory.connection.frames[0]);
  const header = connector.ZlinkStreamHeaderCodec.decode(frame.header);
  assert.equal(header.codec, connector.ZlinkStreamCodec.Json);
  assert.equal(header.name, 'Ready');
  assert.equal(header.metadata.get('trace'), 'json-1');
  assert.deepEqual(JSON.parse(new TextDecoder().decode(frame.payload)), { ready: true });
});

test('stream connector json request wrapper decodes json reply payload', async () => {
  const transportFactory = new MemoryTransportFactory();
  const instance = connector.zlinkStreamConnectorFactory.create({
    endpoint: 'tcp://127.0.0.1:19000',
    transportFactory
  });

  await instance.connect();
  const pending = json.requestJson(instance, { join: true }).packetName('Join').timeout(1000).submit();

  const requestFrame = connector.ZlinkStreamFrameCodec.decode(transportFactory.connection.frames[0]);
  const requestHeader = connector.ZlinkStreamHeaderCodec.decode(requestFrame.header);
  transportFactory.connection.pushFrame(connector.ZlinkStreamFrameCodec.encode(
    connector.ZlinkStreamHeaderCodec.encode({
      kind: connector.ZlinkStreamMessageKind.Response,
      codec: connector.ZlinkStreamCodec.Json,
      flags: connector.ZlinkStreamHeaderFlags.HasRequestSeq,
      requestSeq: requestHeader.requestSeq,
      name: 'JoinReply',
      metadata: connector.ZlinkStreamMetadataMap.empty
    }),
    new TextEncoder().encode('{"accepted":true}')
  ));

  await instance.dispatch();
  assert.deepEqual(await pending, { accepted: true });
});

test('stream connector json on wrapper dispatches typed payloads', async () => {
  const transportFactory = new MemoryTransportFactory();
  const instance = connector.zlinkStreamConnectorFactory.create({
    endpoint: 'tcp://127.0.0.1:19000',
    transportFactory
  });
  const received = [];

  json.onJson(instance, 'Notice', (message) => {
    received.push(message.payload);
  });

  await instance.connect();
  transportFactory.connection.pushFrame(connector.ZlinkStreamFrameCodec.encode(
    connector.ZlinkStreamHeaderCodec.encode({
      kind: connector.ZlinkStreamMessageKind.Send,
      codec: connector.ZlinkStreamCodec.Json,
      flags: connector.ZlinkStreamHeaderFlags.None,
      name: 'Notice',
      metadata: connector.ZlinkStreamMetadataMap.empty
    }),
    new TextEncoder().encode('{"notice":1}')
  ));

  await instance.dispatch();
  assert.deepEqual(received, [{ notice: 1 }]);
});

class MemoryTransportFactory {
  constructor() {
    this.connection = new MemoryConnection();
  }

  async connect() {
    return this.connection;
  }
}

class MemoryConnection {
  constructor() {
    this.frames = [];
    this.inbound = [];
  }

  async write(frame) {
    this.frames.push(frame);
  }

  async read() {
    return this.inbound.shift();
  }

  pushFrame(frame) {
    this.inbound.push(frame);
  }

  async close() {}
}
