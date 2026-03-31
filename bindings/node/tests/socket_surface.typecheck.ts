import * as zlink from '../dist/index';

const ctx = new zlink.Context();

const pub = new zlink.PubSocket(ctx);
pub.publish('topic', zlink.Message.copyOf('ok'));
pub.tryPublish('topic', zlink.Message.copyOf('ok'));
// @ts-expect-error pub cannot receive
pub.receive();
// @ts-expect-error pub cannot subscribe
pub.subscribe();
// @ts-expect-error pub cannot use generic send
pub.send(zlink.Message.copyOf('ok'));
// @ts-expect-error canonical pub does not expose raw sockopt
pub.setSockOpt(zlink.SocketOption.LINGER, Buffer.alloc(4));
// @ts-expect-error canonical pub does not expose generic option api
pub.setOption(zlink.SocketOption.LINGER, Buffer.alloc(4));

const sub = new zlink.SubSocket(ctx);
sub.setSubscription('topic');
sub.subscribe();
sub.trySubscribe();
sub.subscribeHandler(() => {});
// @ts-expect-error sub cannot send
sub.send(zlink.Message.copyOf('bad'));
// @ts-expect-error sub cannot set routing id
sub.setRoutingId(Buffer.from('id'));

const dealer = new zlink.DealerSocket(ctx);
dealer.send(zlink.Message.copyOf('ok'));
dealer.trySend(zlink.Message.copyOf('ok'));
dealer.receive();
dealer.tryReceive();
dealer.recvHandler(() => {});
dealer.setRoutingId(Buffer.from('id'));
dealer.getRoutingId();
// @ts-expect-error dealer has no subscribe
dealer.subscribe();
// @ts-expect-error dealer has no generic option api
dealer.setOption(zlink.SocketOption.LINGER, Buffer.alloc(4));

const router = new zlink.RouterSocket(ctx);
router.receive();
router.tryReceive();
router.recvHandler(() => {});
router.setRoutingId(Buffer.from('id'));
router.getRoutingId();
router.send(Buffer.from('id'), zlink.Message.copyOf('ok'));
router.trySend(Buffer.from('id'), [zlink.Message.copyOf('ok')]);
// @ts-expect-error router has no subscribe
router.subscribe();
// @ts-expect-error router has no generic sockopt api
router.setSockOpt(zlink.SocketOption.LINGER, Buffer.alloc(4));

const stream = new zlink.StreamSocket(ctx);
stream.send(Buffer.from('id'), zlink.Message.copyOf('ok'));
stream.trySend(Buffer.from('id'), zlink.Message.copyOf('ok'));
stream.receive();
stream.tryReceive();
stream.recvHandler(() => {});
// @ts-expect-error canonical stream has no streamAttach helper
stream.streamAttach(() => {});
// @ts-expect-error canonical stream has no generic option api
stream.setOption(zlink.SocketOption.LINGER, Buffer.alloc(4));

const xpub = new zlink.XPubSocket(ctx);
xpub.publish('topic', zlink.Message.copyOf('ok'));
xpub.tryPublish('topic', zlink.Message.copyOf('ok'));
xpub.receiveSubscriptionEvent();
xpub.tryReceiveSubscriptionEvent();
xpub.setVerbose(true);
xpub.setVerboser(true);
xpub.setNoDrop(true);
// @ts-expect-error xpub cannot subscribe
xpub.subscribe();
// @ts-expect-error xpub cannot use generic send
xpub.send(zlink.Message.copyOf('ok'));

const spotNode = new zlink.SpotNode(ctx);
const spot = new zlink.Spot(spotNode);
spot.publish('topic', zlink.Message.copyOf('ok'));
spot.tryPublish('topic', [zlink.Message.copyOf('ok')]);
spot.setSubscription('topic');
spot.unsetSubscription('topic');
spot.subscribe();
spot.trySubscribe();
spot.subscribeHandler(() => {});
// @ts-expect-error spot does not expose recv
spot.recv();

const monitor = pub.monitorOpen(zlink.MonitorEvent.ALL);
monitor.recv();
monitor.tryRecv();
monitor.close();

const serviceMonitor = spot.openMonitor();
serviceMonitor.recv();
serviceMonitor.tryRecv();
serviceMonitor.close();

ctx.close();
