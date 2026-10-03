// Machine naming convention.
//
// Ids are "<room>-<type><number>", e.g. sj-w1 is Washer 1 and sj-d1 is Dryer 1
// in the St Jerome's room. The type is in the id, and the number is the number
// printed on the machine's sticker, counting from the door.
//
//   sj-w1  -> Washer 1     sj-d1  -> Dryer 1      (far end)
//   sj-w2  -> Washer 2     sj-d2  -> Dryer 2
//   ...                    ...
//   sj-w10 -> Washer 10    sj-d10 -> Dryer 10     (nearest the door)
//
// This replaced an older scheme (a1-m1 .. a1-m20) where odd ids were washers,
// even ids were dryers, and the numbering ran the opposite way to the stickers.

export const WASHER = 'washer';
export const DRYER = 'dryer';

// Machine id prefix for each room. Mirrors the areaToRoomMap in api/machines.js.
const ROOM_PREFIXES = {
  'SJU-Sieg/Ryan': 'sj',
  'SJU-Finn': 'fn',
};

// How many washer/dryer pairs a room has.
const ROOM_PAIR_COUNTS = {
  'SJU-Sieg/Ryan': 10,
};

const DEFAULT_PAIR_COUNT = 10;

/** Parse an id, e.g. "sj-w3" -> { prefix: 'sj', type: 'washer', number: 3 }. */
export function parseMachineId(machineId) {
  const match = /^([a-z]+)-([wd])(\d+)$/i.exec(machineId || '');
  if (!match) return null;
  return {
    prefix: match[1].toLowerCase(),
    type: match[2].toLowerCase() === 'w' ? WASHER : DRYER,
    number: parseInt(match[3], 10),
  };
}

// Every machine in the St Jerome's room. Only these appear on /test and /test/history.
export const TRIAL_MACHINE_IDS = Array.from({ length: 10 }, (_, i) => [`sj-w${i + 1}`, `sj-d${i + 1}`]).flat();

/** The number printed on the machine's sticker, e.g. "sj-w3" -> 3. */
export function machineNumberFromId(machineId) {
  return parseMachineId(machineId)?.number ?? null;
}

/** Washer or dryer, taken straight from the id. */
export function typeForMachineId(machineId) {
  return parseMachineId(machineId)?.type ?? null;
}

/** Display name, e.g. "sj-w3" -> "Washer 3". */
export function labelForMachineId(machineId) {
  const parsed = parseMachineId(machineId);
  if (!parsed) return machineId;
  return `${parsed.type === WASHER ? 'Washer' : 'Dryer'} ${parsed.number}`;
}

export function roomPrefix(roomName) {
  return ROOM_PREFIXES[roomName] || null;
}

export function roomPairCount(roomName) {
  return ROOM_PAIR_COUNTS[roomName] ?? DEFAULT_PAIR_COUNT;
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

  const pairs = roomPairCount(roomName);
  const now = Date.now();
  const slots = [];

  for (let number = 1; number <= pairs; number++) {
    for (const type of [WASHER, DRYER]) {
      const id = `${prefix}-${type === WASHER ? 'w' : 'd'}${number}`;
      const status = statuses[id];
      const report = reports[id];

      const lastUpdate = status?.lastUpdate ? new Date(status.lastUpdate) : null;
      const hasSensor = Boolean(status) && lastUpdate !== null;
      const ageMs = hasSensor ? now - lastUpdate.getTime() : null;

      slots.push({
        id,
        number,
        type,
        // Where the card sits on the page: walking order from the door.
        displayIndex: number,
        typeIndex: number,
        label: `${type === WASHER ? 'Washer' : 'Dryer'} ${number}`,

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
