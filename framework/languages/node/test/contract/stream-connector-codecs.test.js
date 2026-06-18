const assert = require('node:assert/strict');
const test = require('node:test');

const connector = require('../../packages/stream-connector/dist');
const msgpack = require('../../packages/framework-codec-msgpack/dist');
const protobuf = require('../../packages/framework-codec-protobuf/dist');

test('stream connector messagepack codec encodes and decodes payloads', () => {
  const payload = msgpack.toMsgPack({ ready: true });

  assert.equal(payload.codec, connector.ZlinkStreamCodec.MessagePack);
  assert.notEqual(Buffer.from(payload.payload).toString('utf8'), '{"ready":true}');
  assert.deepEqual(msgpack.fromMsgPack(payload), { ready: true });
  assert.throws(
    () => msgpack.fromMsgPack({ codec: connector.ZlinkStreamCodec.Json, payload: new Uint8Array() }),
    /not MessagePack/
  );
});

test('stream connector messagepack codec decodes replies through connector', async () => {
  const transportFactory = new MemoryTransportFactory();
  const instance = connector.zlinkStreamConnectorFactory.create({
    endpoint: 'tcp://127.0.0.1:19000',
    transportFactory,
    codec: msgpack.zlinkStreamMessagePackCodec
  });

  await instance.connect();
  const pending = instance.request(new Join()).timeout(1000).submit();

  const requestFrame = connector.ZlinkStreamFrameCodec.decode(transportFactory.connection.frames[0]);
  const requestHeader = connector.ZlinkStreamHeaderCodec.decode(requestFrame.header);
  transportFactory.connection.pushFrame(connector.ZlinkStreamFrameCodec.encode(
    connector.ZlinkStreamHeaderCodec.encode({
      kind: connector.ZlinkStreamMessageKind.Response,
      codec: connector.ZlinkStreamCodec.MessagePack,
      flags: connector.ZlinkStreamHeaderFlags.HasRequestSeq,
      requestSeq: requestHeader.requestSeq,
      name: 'JoinReply',
      metadata: connector.ZlinkStreamMetadataMap.empty
    }),
    msgpack.toMsgPack({ accepted: true }).payload
  ));

  await instance.dispatch();
  assert.deepEqual(await pending, { accepted: true });
});

test('stream connector protobuf codec uses supplied protobuf type', () => {
  const type = createLengthPrefixedJsonType();
  const payload = protobuf.toProto({ ready: true }, type);

  assert.equal(payload.codec, connector.ZlinkStreamCodec.Protobuf);
  assert.deepEqual(protobuf.fromProto(payload, type), { ready: true });
  assert.throws(
    () => protobuf.fromProto({ codec: connector.ZlinkStreamCodec.Raw, payload: new Uint8Array() }, type),
    /not Protobuf/
  );
});

test('stream connector protobuf codec dispatches typed payloads through connector', async () => {
  const type = createLengthPrefixedJsonType();
  const transportFactory = new MemoryTransportFactory();
  const instance = connector.zlinkStreamConnectorFactory.create({
    endpoint: 'tcp://127.0.0.1:19000',
    transportFactory,
    codec: protobuf.createZlinkStreamProtobufCodec(type)
  });
  const received = [];

  instance.on('Notice', (message) => {
    received.push(message.payload);
  });

  await instance.connect();
  transportFactory.connection.pushFrame(connector.ZlinkStreamFrameCodec.encode(
    connector.ZlinkStreamHeaderCodec.encode({
      kind: connector.ZlinkStreamMessageKind.Send,
      codec: connector.ZlinkStreamCodec.Protobuf,
      flags: connector.ZlinkStreamHeaderFlags.None,
      name: 'Notice',
      metadata: connector.ZlinkStreamMetadataMap.empty
    }),
    protobuf.toProto({ notice: 1 }, type).payload
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

class Join {
  constructor() {
    this.join = true;
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

function createLengthPrefixedJsonType() {
  return {
    encode(value) {
      const json = new TextEncoder().encode(JSON.stringify(value));
      return {
        finish() {
          const bytes = new Uint8Array(4 + json.length);
          bytes[0] = (json.length >>> 24) & 0xff;
          bytes[1] = (json.length >>> 16) & 0xff;
          bytes[2] = (json.length >>> 8) & 0xff;
          bytes[3] = json.length & 0xff;
          bytes.set(json, 4);
          return bytes;
        }
      };
    },
    decode(bytes) {
      const length = bytes[0] * 0x1000000 + ((bytes[1] << 16) | (bytes[2] << 8) | bytes[3]);
      return JSON.parse(new TextDecoder().decode(bytes.subarray(4, 4 + length)));
    }
  };
}
