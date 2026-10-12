import * as THREE from "three";

function planarDistance(first, second) {
  return Math.hypot(first.x - second.x, first.y - second.y);
}

function circleIntersection(first, firstRadius, second, secondRadius, branch) {
  const distance = planarDistance(first, second);
  if (distance < 1e-8 || distance > firstRadius + secondRadius || distance < Math.abs(firstRadius - secondRadius)) return null;
  const along = (firstRadius ** 2 - secondRadius ** 2 + distance ** 2) / (2 * distance);
  const height = Math.sqrt(Math.max(0, firstRadius ** 2 - along ** 2));
  const horizontal = (second.x - first.x) / distance;
  const vertical = (second.y - first.y) / distance;
  return new THREE.Vector3(first.x + horizontal * along - branch * vertical * height,
    first.y + vertical * along + branch * horizontal * height, first.z);
}

function snapshot(object) {
  return { object, position: object.position.clone(), quaternion: object.quaternion.clone() };
}

function transformPart(part, originalPivot, newPivot, angle, rotateOrientation = true) {
  const horizontal = part.position.x - originalPivot.x;
  const vertical = part.position.y - originalPivot.y;
  part.object.position.set(newPivot.x + horizontal * Math.cos(angle) - vertical * Math.sin(angle),
    newPivot.y + horizontal * Math.sin(angle) + vertical * Math.cos(angle), part.position.z);
  if (rotateOrientation) part.object.quaternion.copy(new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(0, 0, 1), angle).multiply(part.quaternion));
}

export function createCadLegRig(model, side, createWheel) {
  const names = side === "left" ? ["TALOS_Left_Leg_Link-1", "Left_Wheel_Leg-1", "Left_Leg_Support-1", "support_clips_-_Left-1", "Right_Wheel_Clip_-2"]
    : ["TALOS_Right_Leg_Link-1", "Right_Wheel_Leg_-2", "Right_Leg_Support-1", "support_clips_-_Right-1", "Left_Wheel_Clip-2"];
  const [upper, lower, support, supportClip, wheelClip] = names.map((name) => model.getObjectByName(name));
  if ([upper, lower, support, supportClip, wheelClip].some((part) => !part)) throw new Error(`Missing CAD parts for ${side} leg`);
  model.updateMatrixWorld(true);
  const localPoint = (part, vertical) => model.worldToLocal(part.localToWorld(new THREE.Vector3(0, vertical, 0)));
  const drivePivot = localPoint(upper, 0.1);
  const supportPivot = localPoint(support, 0.077725947);
  const driveKnee = localPoint(upper, 0);
  const supportKnee = localPoint(lower, 0);
  const driveRadius = planarDistance(drivePivot, driveKnee);
  const supportRadius = planarDistance(supportPivot, supportKnee);
  const couplerRadius = planarDistance(driveKnee, supportKnee);
  const cross = (supportPivot.x - driveKnee.x) * (supportKnee.y - driveKnee.y) -
    (supportPivot.y - driveKnee.y) * (supportKnee.x - driveKnee.x);
  const branch = cross < 0 ? -1 : 1;
  const referenceAngle = Math.atan2(driveKnee.x - drivePivot.x, drivePivot.y - driveKnee.y);
  const lowerReferenceAngle = Math.atan2(driveKnee.y - supportKnee.y, driveKnee.x - supportKnee.x);
  const supportReferenceAngle = Math.atan2(supportKnee.y - supportPivot.y, supportKnee.x - supportPivot.x);
  const wheel = createWheel();
  wheel.position.copy(localPoint(lower, -0.12408));
  wheel.position.z += side === "left" ? -0.025 : 0.025;
  model.add(wheel);
  const parts = [upper, lower, support, supportClip, wheelClip, wheel].map(snapshot);

  const solve = (angle) => {
    const knee = drivePivot.clone().add(new THREE.Vector3(Math.sin(angle) * driveRadius, -Math.cos(angle) * driveRadius, 0));
    const follower = circleIntersection(knee, couplerRadius, supportPivot, supportRadius, branch);
    if (!follower) return null;
    const lowerAngle = Math.atan2(knee.y - follower.y, knee.x - follower.x) - lowerReferenceAngle;
    const wheelVector = parts[5].position.clone().sub(supportKnee);
    const wheelHeight = follower.y + wheelVector.x * Math.sin(lowerAngle) + wheelVector.y * Math.cos(lowerAngle);
    return { knee, follower, lowerAngle, wheelHeight };
  };
  let extendedAngle = referenceAngle;
  let minimumWheelHeight = Infinity;
  for (let degrees = -30; degrees <= 150; degrees += 0.1) {
    const angle = THREE.MathUtils.degToRad(degrees);
    const solution = solve(angle);
    if (solution && solution.wheelHeight < minimumWheelHeight) {
      extendedAngle = angle;
      minimumWheelHeight = solution.wheelHeight;
    }
  }

  function applyAngle(angle) {
    const solution = solve(angle);
    if (!solution) return false;
    const driveRotation = angle - referenceAngle;
    const supportRotation = Math.atan2(solution.follower.y - supportPivot.y,
      solution.follower.x - supportPivot.x) - supportReferenceAngle;
    transformPart(parts[0], drivePivot, drivePivot, driveRotation);
    transformPart(parts[1], supportKnee, solution.follower, solution.lowerAngle);
    transformPart(parts[2], supportPivot, supportPivot, supportRotation);
    transformPart(parts[3], supportKnee, solution.follower, supportRotation);
    transformPart(parts[4], drivePivot, drivePivot, driveRotation);
    transformPart(parts[5], supportKnee, solution.follower, solution.lowerAngle, false);
    return true;
  }
  return {
    names,
    referenceAngleDeg: THREE.MathUtils.radToDeg(referenceAngle),
    estimatedExtendedAngleDeg: THREE.MathUtils.radToDeg(extendedAngle),
    canBend: (degrees) => Number.isFinite(degrees) && Boolean(solve(extendedAngle + THREE.MathUtils.degToRad(degrees))),
    setBend: (degrees) => applyAngle(extendedAngle + THREE.MathUtils.degToRad(degrees)),
    restoreReference: () => parts.forEach((part) => {
      part.object.position.copy(part.position);
      part.object.quaternion.copy(part.quaternion);
    }),
    applyReference: () => applyAngle(referenceAngle),
  };
}
