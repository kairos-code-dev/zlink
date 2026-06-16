const assert = require('node:assert/strict');
const test = require('node:test');

const zlink = require('../../../../../bindings/node/dist');
const connector = require('../../packages/stream-connector/dist');
const framework = require('../../packages/framework/dist/internal');

test('Message.from(class instance) supplies packetName to channel send envelopes', async () => {
  class GetProfileReq {
    constructor(id) {
      this.id = id;
    }
  }

  const ctx = zlink.createContext();
  const router = zlink.createRouterSocket(ctx);
  const dealer = zlink.createDealerSocket(ctx);
  const endpoint = `inproc://message-packet-name-${process.pid}-${Date.now()}`;
  const registration = framework.createFrameworkRegistration({
    channels: {
      api: { client: { manualConnections: [endpoint] } }
    }
  });
  const client = new framework.DefaultZLinkChannelClient(
    registration,
    new framework.ZLinkDealerChannelClientTransport(dealer)
  );

  try {
    router.bind(endpoint);
    dealer.connect(endpoint);

    await client.sendToChannel('api', zlink.Message.from(new GetProfileReq(7))).submit();

    const received = await recvRouterMessage(router);
    const envelope = decodeDotnetEnvelope(received.parts);
    assert.equal(envelope.header.channelName, 'api');
    assert.equal(envelope.header.messageName, 'GetProfileReq');
    assert.deepEqual(envelope.body, { id: 7 });
    received.close();
  } finally {
    dealer.close();
    router.close();
    ctx.close();
  }
});

test('Message.from(class instance) supplies packetName to stream send calls', async () => {
  class Ready {
    constructor(ok) {
      this.ok = ok;
    }
  }

  const written = [];
  const runtime = new framework.ZLinkStreamBindingRuntime({
    messageFactory: {
      createTextMessage(payload) {
        return { payload, close() {} };
      },
      createBinaryMessage(payload) {
        return { bytes: payload, close() {} };
      }
    }
  });
  const context = runtime.createSessionContext({
    sessionId: 'session-message-name',
    routingId: 'rid-message-name',
    localAddr: undefined,
    remoteAddr: undefined,
    write(message) {
      written.push(message.bytes);
      return true;
    },
    async close() {}
  });

  await context.client.send(zlink.Message.from(new Ready(true))).submit();

  assert.equal(written.length, 1);
  const frame = connector.ZlinkStreamFrameCodec.decode(written[0]);
  const header = connector.ZlinkStreamHeaderCodec.decode(frame.header);
  assert.equal(header.name, 'Ready');
});

async function recvRouterMessage(router) {
  const deadline = Date.now() + 1000;
  while (Date.now() < deadline) {
    const received = new zlink.Received();
    if (router.recv(received, zlink.RecvFlags.DontWait)) {
      return received;
    }
    await new Promise((resolve) => setImmediate(resolve));
  }
  assert.fail('router did not receive message');
}

function decodeDotnetEnvelope(parts) {
  assert.equal(parts.length, 2);
  return {
    header: JSON.parse(parts[0].data().toString()),
    body: JSON.parse(parts[1].data().toString())
  };
}
