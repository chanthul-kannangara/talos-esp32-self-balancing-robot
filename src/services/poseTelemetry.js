export function wrapDegrees(degrees) {
  return ((degrees + 180) % 360 + 360) % 360 - 180;
}

export function parseUsbPose(line, receivedAt = performance.now()) {
  let frame;
  try { frame = JSON.parse(line); } catch { return null; }
  if (!frame || frame.talosPose !== 1 || [frame.boot, frame.seq, frame.us].some(value =>
      !Number.isInteger(value) || value < 0 || value > 0xffffffff) ||
      !Number.isFinite(frame.hz) || frame.hz < 0 || !Number.isFinite(frame.p)) return null;
  for (const [key, length] of [["a", 4], ["e", 6], ["c", 5], ["m", 4], ["h", 5]]) {
    if (!Array.isArray(frame[key]) || frame[key].length !== length || !frame[key].every(Number.isFinite)) return null;
  }
  if (frame.e.slice(0, 2).some(value => !Number.isInteger(value) || value < 0 || value > 4095) ||
      frame.e.slice(2, 4).some(value => !Number.isInteger(value) || value < 0 || value > 255) ||
      frame.e.slice(4).some(value => value < 0) || frame.h[0] < 0 || frame.m.some(value => value < 500 || value > 2500) ||
      frame.c.slice(3).some(value => value !== 1 && value !== -1) ||
      [frame.c[0], ...frame.h.slice(1)].some(value => value !== 0 && value !== 1) ||
      frame.c.slice(1, 3).some(value => value < 0 || value >= 360) ||
      frame.a.slice(0, 2).some(value => Math.abs(value) > 180)) return null;
  const magneticGood = (value) => (value & 0x20) !== 0 && (value & 0x18) === 0;
  return {
    online: true, poseTransport: "usb", poseReceivedAtMs: receivedAt, poseBootId: frame.boot,
    poseSequence: frame.seq, deviceSampleUs: frame.us, tilt: frame.a[0], roll: frame.a[1],
    gyroY: frame.a[2], rollRate: frame.a[3], leftEncoderDeg: frame.e[0] * 360 / 4096,
    rightEncoderDeg: frame.e[1] * 360 / 4096, leftMagneticStatus: frame.e[2], rightMagneticStatus: frame.e[3],
    leftEncoderSampleAgeMs: frame.e[4], rightEncoderSampleAgeMs: frame.e[5],
    extendedHeightCaptured: frame.c[0] === 1, leftExtendedEncoderDeg: frame.c[1], rightExtendedEncoderDeg: frame.c[2],
    leftEncoderSign: frame.c[3], rightEncoderSign: frame.c[4], wheelLeftUs: frame.m[0],
    wheelRightUs: frame.m[1], legLeftUs: frame.m[2], legRightUs: frame.m[3],
    imuSampleAgeMs: frame.h[0], imuSampleHealthy: frame.h[1] === 1 && frame.h[0] < 20,
    balancingEnabled: frame.h[2] === 1, standingEnabled: frame.h[3] === 1, benchMode: frame.h[4] === 1,
    encodersHealthy: magneticGood(frame.e[2]) && magneticGood(frame.e[3]) && frame.e[4] < 100 && frame.e[5] < 100,
    jointSupported: true, measuredControlHz: frame.hz, pidOutput: frame.p,
  };
}

export function createVisualPose(status, localReference = null, diagnosticEnabled = false) {
  const referenceMatches = localReference && Number.isInteger(status.poseBootId) && localReference.bootId === status.poseBootId;
  const calibrated = status.extendedHeightCaptured === true;
  const result = { ...status, visualLegPoseAvailable: false, visualLegPoseTrusted: false, visualLegPoseDiagnostic: false };
  let magneticFieldsHealthy = true;
  for (const side of ["left", "right"]) {
    const encoder = Number(status[`${side}EncoderDeg`]);
    const magnetic = Number(status[`${side}MagneticStatus`]);
    const age = Number(status[`${side}EncoderSampleAgeMs`]);
    if (!status.online || !Number.isFinite(encoder) || !Number.isFinite(magnetic) ||
        encoder < 0 || encoder >= 360 || !Number.isFinite(age) || age < 0 || age >= 100 || (magnetic & 0x20) === 0) return result;
    magneticFieldsHealthy = magneticFieldsHealthy && (magnetic & 0x18) === 0;
  }
  const trusted = calibrated && status.encodersHealthy && magneticFieldsHealthy;
  const diagnostic = diagnosticEnabled && (referenceMatches || calibrated);
  if (!trusted && !diagnostic) return result;
  for (const side of ["left", "right"]) {
    const reference = calibrated ? Number(status[`${side}ExtendedEncoderDeg`]) : localReference[side];
    const sign = Number(status[`${side}EncoderSign`]);
    if (!Number.isFinite(reference) || (sign !== 1 && sign !== -1)) return result;
    result[`${side}BendDeg`] = sign * wrapDegrees(reference - Number(status[`${side}EncoderDeg`]));
  }
  result.visualLegPoseAvailable = true;
  result.visualLegPoseTrusted = Boolean(trusted);
  result.visualLegPoseDiagnostic = !trusted;
  return result;
}

export function allPoseOutputsNeutral(status) {
  return !status.balancingEnabled && !status.standingEnabled && !status.benchMode &&
    ["wheelLeftUs", "wheelRightUs", "legLeftUs", "legRightUs"].every(key => Number(status[key]) === 1500);
}
