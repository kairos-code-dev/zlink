const assert = require('node:assert/strict');
const path = require('node:path');
const connector = require('../../../packages/stream-connector/dist');
const json = require('../../../packages/stream-connector-json/dist');
const { reserveTcpEndpoint, withServers } = require('../../shared/process-host');

async function main() {
  const endpoint = await reserveTcpEndpoint();
  await withServers([
    {
      entry: path.resolve(__dirname, '../server/main.js'),
      env: { STREAMING_CLIENT_ENDPOINT: endpoint }
    }
  ], async () => {
    const client = connector.zlinkStreamConnectorFactory.create({
      endpoint,
      dispatchMode: connector.ZlinkStreamDispatchMode.Manual,
      heartbeat: { enabled: false }
    });
    const notifications = [];
    client.on('ServerNotice', (message) => {
      notifications.push(json.fromJson(message.payload).message);
    });
    try {
      await client.connect();
      const replyTask = client
        .request(json.toJson({ playerId: 'p1' }))
        .packetName('Join')
        .timeout(1000)
        .submit();
      await client.dispatch();
      await client.dispatch();
      const reply = json.fromJson(await replyTask);
      assert.deepEqual(reply, { accepted: true, playerId: 'p1' });
      assert.deepEqual(notifications, ['welcome']);
    } finally {
      await client.close();
    }
  });

  console.log('PASS StreamingClient');
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
