// Machine naming convention.
//
// Sensors were assigned ids a1-m1 .. a1-m20 in one direction, but the physical
// stickers already on the machines in room a1 run the OTHER way. The pair you
// meet first walking in (a1-m1 / a1-m2) is stickered 10, and the pair at the far
// end (a1-m19 / a1-m20) is stickered 1. Odd ids are washers, even ids are
// dryers, and the number is mirrored:
//
//   a1-m1  -> Washer 10    a1-m2  -> Dryer 10
//   a1-m3  -> Washer 9     a1-m4  -> Dryer 9
//   ...                    ...
//   a1-m19 -> Washer 1     a1-m20 -> Dryer 1
//
// Cards stay ordered by machine id, which puts the 10s at the top of the page
// and counts down to 1 at the bottom -- the order you actually walk past them.
//
// The mirroring describes how this one room was stickered, not a universal
// rule, so it is opt-in per room rather than baked into the maths.

export const WASHER = 'washer';
export const DRYER = 'dryer';

// Machine id prefix for each room. Mirrors the areaToRoomMap in api/machines.js.
const ROOM_PREFIXES = {
  'SJU-Sieg/Ryan': 'a1',
  'SJU-Finn': 'a2',
};

// How many machine slots each room has, so we can render placeholders for
// machines that have never reported in.
// TODO: move this onto the rooms collection (room.machineIds) once a second
// room is actually wired up with sensors.
const ROOM_MACHINE_COUNTS = {
  'SJU-Sieg/Ryan': 20,
};

const DEFAULT_MACHINE_COUNT = 20;

// Rooms whose stickers number machines in the opposite direction to their ids.
const ROOMS_WITH_MIRRORED_LABELS = new Set(['SJU-Sieg/Ryan']);

/** Extract the trailing machine number from an id, e.g. "a1-m3" -> 3. */
export function machineNumberFromId(machineId) {
  const match = /m(\d+)$/.exec(machineId || '');
  return match ? parseInt(match[1], 10) : null;
}

/** Odd numbers are washers, even numbers are dryers. */
export function typeForNumber(number) {
  return number % 2 === 1 ? WASHER : DRYER;
}

/**
 * Position in walking order, counting from the door: m1/m2 -> 1, m3/m4 -> 2,
 * ... m19/m20 -> 10. This drives where a card sits on the page.
 */
export function displayIndexForNumber(number) {
  return Math.ceil(number / 2);
}

/**
 * The number actually printed on the machine's sticker. In a mirrored room the
 * machine nearest the door is the highest number, so the index counts down.
 */
export function typeIndexForNumber(number, { perType = 10, mirrored = false } = {}) {
  const position = displayIndexForNumber(number);
  return mirrored ? perType + 1 - position : position;
}

/** Display name, e.g. 5 -> "Washer 8" in a mirrored room of 20. */
export function labelForNumber(number, options) {
  const noun = typeForNumber(number) === WASHER ? 'Washer' : 'Dryer';
  return `${noun} ${typeIndexForNumber(number, options)}`;
}

export function labelForMachineId(machineId, options) {
  const number = machineNumberFromId(machineId);
  return number === null ? machineId : labelForNumber(number, options);
}

export function roomPrefix(roomName) {
  return ROOM_PREFIXES[roomName] || null;
}

export function roomMachineCount(roomName) {
  return ROOM_MACHINE_COUNTS[roomName] ?? DEFAULT_MACHINE_COUNT;
}

/** Labelling rules for a room: how many of each type, and which way they count. */
export function roomLabelOptions(roomName) {
  return {
    perType: Math.ceil(roomMachineCount(roomName) / 2),
    mirrored: ROOMS_WITH_MIRRORED_LABELS.has(roomName),
  };
}

// A reading counts as current within this window. Beyond it we still show the
// machine's last known state, just labelled with its age.
const FRESH_WINDOW_MS = 15 * 60 * 1000;

// How long a "running" reading stays believable. The washer firmware hard-caps
// a cycle at 42 minutes (MAX_CYCLE_MS), and dryers are shorter, so anything
// older than this has finished no matter what the last packet said. Unlike
// running, empty/full does not expire -- it stays true until someone opens the
// door, so we keep showing the last known value indefinitely.
const RUNNING_SHELF_LIFE_MS = 45 * 60 * 1000;

/**
 * Build the full slot list for a room: every machine the room is configured to
 * have, whether or not a sensor has ever reported for it.
 *
 * A machine that has reported at any point always shows its last known state --
 * stale data is still information, and a washer that was empty five hours ago is
 * very likely still empty. Only slots that have never reported have nothing to
 * show.
 *
 * @param {string} roomName
 * @param {object} statuses     keyed by machineId, from GET /api/machines
 * @param {object} reports      keyed by machineId, from GET /api/reports
 * @returns {Array} slots in walking order, nearest the door first
 */
export function buildRoomSlots(roomName, statuses = {}, reports = {}) {
  const prefix = roomPrefix(roomName);
  if (!prefix) return [];

  const count = roomMachineCount(roomName);
  const labelOptions = roomLabelOptions(roomName);
  const now = Date.now();
  const slots = [];

  for (let number = 1; number <= count; number++) {
    const id = `${prefix}-m${number}`;
    const status = statuses[id];
    const report = reports[id];

    const lastUpdate = status?.lastUpdate ? new Date(status.lastUpdate) : null;
    const hasSensor = Boolean(status) && lastUpdate !== null;
    const ageMs = hasSensor ? now - lastUpdate.getTime() : null;

    slots.push({
      id,
      number,
      type: typeForNumber(number),
      // Where the card sits on the page: walking order from the door.
      displayIndex: displayIndexForNumber(number),
      // The number on the machine's sticker, which in this room counts the
      // opposite way to displayIndex.
      typeIndex: typeIndexForNumber(number, labelOptions),
      label: labelForNumber(number, labelOptions),

      // No sensor has ever reported for this slot -- there is genuinely
      // nothing to display.
      hasSensor,
      // Reading is recent enough to present without qualification.
      isFresh: hasSensor && ageMs < FRESH_WINDOW_MS,
      ageMs,
      lastUpdate,

      isRunning: Boolean(status?.running) && hasSensor && ageMs < RUNNING_SHELF_LIFE_MS,
      isEmpty: Boolean(status?.empty),

      flagged: Boolean(report?.flagged),
      flaggedUntil: report?.until ? new Date(report.until) : null,
      brokenCount: report?.brokenCount || 0,
      fixedCount: report?.fixedCount || 0,
    });
  }

  return slots;
}

/** Compact relative age, e.g. "6 min", "5 h", "13 d". */
export function formatAge(ageMs) {
  if (ageMs === null || ageMs === undefined) return null;

  const minutes = Math.round(ageMs / 60000);
  if (minutes < 60) return `${minutes} min`;

  const hours = Math.round(minutes / 60);
  if (hours < 24) return `${hours} h`;

  return `${Math.round(hours / 24)} d`;
}

/** Split slots into the two columns the grid renders, each in walking order. */
export function splitByType(slots) {
  const byPosition = (a, b) => a.displayIndex - b.displayIndex;
  return {
    washers: slots.filter((s) => s.type === WASHER).sort(byPosition),
    dryers: slots.filter((s) => s.type === DRYER).sort(byPosition),
  };
}

/**
 * Count machines that are not currently running.
 *
 * This deliberately matches exactly what shows a green bar, so the headline
 * number, the label and the card colours all agree. Note that it counts
 * machines that are stopped but still full, and machines reported broken --
 * both are, factually, not running.
 */
export function countNotRunning(slots) {
  return slots.filter((s) => s.hasSensor && !s.isRunning).length;
}
