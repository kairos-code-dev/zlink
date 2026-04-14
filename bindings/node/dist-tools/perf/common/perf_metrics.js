// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const args = require('./perf_args');
const measurement = require('./perf_measurement');
const report = require('./perf_report');
module.exports = {
    ...args,
    ...measurement,
    ...report
};
