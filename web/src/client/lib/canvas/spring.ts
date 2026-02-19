// Spring physics for smooth cursor interpolation
// Critically damped spring with slight underdamping for snappy feel

const STIFFNESS = 1500;
const DAMPING = 80;
const MAX_DT = 0.1; // clamp dt to prevent explosion on tab-back
const EPSILON = 0.01; // position threshold for "at rest"

export interface SpringState {
  x: number;
  y: number;
  targetX: number;
  targetY: number;
  vx: number;
  vy: number;
}

export function createSpring(x: number, y: number): SpringState {
  return { x, y, targetX: x, targetY: y, vx: 0, vy: 0 };
}

export function setSpringTarget(spring: SpringState, x: number, y: number) {
  spring.targetX = x;
  spring.targetY = y;
}

// Tick the spring forward by dt seconds.
// Returns true if the spring is still moving (needs more frames).
export function tickSpring(spring: SpringState, dt: number): boolean {
  dt = Math.min(dt, MAX_DT);

  const dx = spring.targetX - spring.x;
  const dy = spring.targetY - spring.y;

  // Spring force: F = -k * displacement, Damping: F = -d * velocity
  const ax = STIFFNESS * dx - DAMPING * spring.vx;
  const ay = STIFFNESS * dy - DAMPING * spring.vy;

  spring.vx += ax * dt;
  spring.vy += ay * dt;
  spring.x += spring.vx * dt;
  spring.y += spring.vy * dt;

  // Check if at rest
  const moving =
    Math.abs(dx) > EPSILON ||
    Math.abs(dy) > EPSILON ||
    Math.abs(spring.vx) > EPSILON ||
    Math.abs(spring.vy) > EPSILON;

  if (!moving) {
    // Snap to target
    spring.x = spring.targetX;
    spring.y = spring.targetY;
    spring.vx = 0;
    spring.vy = 0;
  }

  return moving;
}
