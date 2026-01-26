import assert from "node:assert/strict";

import {
  clamp,
  formatPercent,
  formatScore,
  nextBackoffDelay,
  riskLabel,
  riskLevel,
} from "../src/utils";

assert.equal(clamp(1.5, 0, 1), 1);
assert.equal(clamp(-1, 0, 1), 0);

assert.equal(formatPercent(0.5), "50.0%");
assert.equal(formatScore(105), "100.0");

assert.equal(riskLevel(0.8), "high");
assert.equal(riskLevel(0.5), "medium");
assert.equal(riskLevel(0.1), "low");

assert.equal(riskLabel(0.8), "High risk");
assert.equal(riskLabel(0.5), "Medium risk");
assert.equal(riskLabel(0.1), "Low risk");

assert.equal(nextBackoffDelay(0), 500);
assert.equal(nextBackoffDelay(1), 1000);
assert.equal(nextBackoffDelay(4), 8000);
assert.equal(nextBackoffDelay(6), 10000);

console.log("utils.test.ts passed");
