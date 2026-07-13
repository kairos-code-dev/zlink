import * as connector from '@zlink-systems/stream-connector';
import { bingoProtobuf } from '../Shared/Contracts/protobuf-browser-codec';
import { AuthenticateReq, MatchBingoReq } from '../Shared/Contracts/bingo-messages.generated';
import { BingoSamplePlayers, PacketNames } from '../Shared/Contracts/messages';
import type { AuthenticateRes, MatchBingoRes } from '../Shared/Contracts/messages';
import { SampleTimings } from './Configuration/sample-names';
import { loadSampleConfig } from './Configuration/sample-config';
import { runBrowserSample } from '../../browser-client-runtime';

async function main(): Promise<void> {
  const config = await loadSampleConfig() as Awaited<ReturnType<typeof loadSampleConfig>> & {
    drainExcludedNodeRid?: string;
    drainGateUrl?: string;
  };
  const excludedNodeRid = config.drainExcludedNodeRid;
  if (excludedNodeRid === undefined || excludedNodeRid.length === 0) {
    throw new Error('BINGO_DRAIN_EXCLUDED_NODE_RID is required.');
  }
  const gateUrl = config.drainGateUrl;
  if (gateUrl === undefined || gateUrl.length === 0) {
    throw new Error('drainGateUrl is required.');
  }
  const client = connector.zlinkStreamConnectorFactory.create({
    endpoint: config.sessionAEndpoint,
    codec: bingoProtobuf,
    dispatchMode: connector.ZlinkStreamDispatchMode.Immediate,
    waitTimeoutMs: SampleTimings.requestTimeout,
    heartbeat: { enabled: false }
  });
  try {
    await client.connect();
    const authenticated = await client
      .request(new AuthenticateReq({ accessToken: BingoSamplePlayers.drainProbe }))
      .packetName(PacketNames.authenticateReq)
      .submit<AuthenticateRes>();
    if (authenticated.actorNodeRid !== excludedNodeRid) {
      throw new Error(
        `Drain probe actor was not created on the node selected for drain: ` +
        `actorNode=${authenticated.actorNodeRid} drainNode=${excludedNodeRid}`
      );
    }
    console.log(`bingo-drain-probe ready actor-node=${authenticated.actorNodeRid}`);
    await waitForGate(gateUrl);
    const matched = await client
      .request(new MatchBingoReq({ mode: 'two-player' }))
      .packetName(PacketNames.matchBingoReq)
      .submit<MatchBingoRes>();
    if (matched.roomOwnerNodeRid === excludedNodeRid) {
      throw new Error(
        `Draining node '${excludedNodeRid}' received new matching allocation: ` +
        `roomOwner=${matched.roomOwnerNodeRid}`
      );
    }
    console.log(
      `bingo-drain-probe actor-node=${authenticated.actorNodeRid} ` +
      `room-owner=${matched.roomOwnerNodeRid} excluded=${excludedNodeRid}`
    );
  } finally {
    await client.close();
  }
}

async function waitForGate(gateUrl: string): Promise<void> {
  for (;;) {
    const response = await fetch(gateUrl, { cache: 'no-store' });
    if (response.status === 204) return;
    await new Promise((resolve) => setTimeout(resolve, 50));
  }
}

void runBrowserSample('Bingo.Ts drain probe', main);
