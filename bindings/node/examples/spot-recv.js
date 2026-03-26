// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('../src');

const ctx = new zlink.Context();
const spot = new zlink.Spot(ctx);
const monitor = spot.openMonitor();

console.log(typeof monitor.recv);

monitor.close();
spot.close();
ctx.close();
