import {assertFalse, forEachScan, math, newInt32Array} from './Mini.js';
import {readNumber} from "./XRangeDecoder.js";
import * as SinCos from './SinCos.js';

let /** @type{number} */ MAX_LEVEL = 7;

let /** @type{number} */ MAX_F1 = 4;
let /** @type{number} */ MAX_F2 = 5;
let /** @type{number} */ MAX_F3 = 5;
let /** @type{number} */ MAX_F4 = 5;
let /** @type{number} */ MAX_LINE_LIMIT = 63;

let /** @type{number} */ MAX_PARTITION_CODE = MAX_F1 * MAX_F2 * MAX_F3 * MAX_F4;
let /** @type{number} */ MAX_COLOR_QUANT_OPTIONS = 17;
let /** @type{number} */ MAX_COLOR_PALETTE_SIZE = 32;
let /** @type{number} */ MAX_COLOR_CODE = MAX_COLOR_QUANT_OPTIONS + MAX_COLOR_PALETTE_SIZE;

/**
 * @param{number} code
 * @return{number}
 */
let makeColorQuant = (code) => 1 + ((4 + (code & 3)) << (code >> 2));

let /** @type{number} */ SCALE_STEP_FACTOR = 40;
let /** @type{number} */ BASE_SCALE_FACTOR = 36;

/** @type{number} */
let _width;
/** @type{number} */
let _height;
/** @type{number} */
let _colorQuant;
/** @type{number} */
let _paletteSize;
/** @type{number} */
let lineLimit;
/** @const @type{!Int32Array} */
let _levelScale = newInt32Array(MAX_LEVEL);
/** @const @type{!Int32Array} */
export let angleBits = newInt32Array(MAX_LEVEL);

/** @return{number} */
export let getWidth = () => _width;
/** @return{number} */
export let getHeight = () => _height;
/** @return{number} */
export let getLineLimit = () => lineLimit;

/**
 * @param{number} w
 * @param{number} h
 * @return{void}
 */
export let initForTest = (w, h) => {
  _width = w;
  _height = h;
};

/**
 * @return{number}
 */
export let getColorQuant = () => _colorQuant;

/**
 * @return{number}
 */
export let getPaletteSize = () => _paletteSize;

/**
 * @return{number}
 */
export let getLineQuant = () => SinCos.SCALE;

/**
 * @return {number}
 */
let readSize = () => {
  let /** @type{number} */ bits = -1;
  while ((bits < 8) || (readNumber(2) > 0)) {
    bits = 8 * bits + readNumber(8) + 8;
  }
  return bits;
};

/** @return{void} */
export let read = () => {
  _width = readSize();
  _height = readSize();

  let /** @type{number} */ f1 = readNumber(MAX_F1);
  let /** @type{number} */ f2 = 2 + readNumber(MAX_F2);
  let /** @type{number} */ f3 = 10 ** (3 - readNumber(MAX_F3) / 5) | 0;
  let /** @type{number} */ f4 = readNumber(MAX_F4);
  lineLimit = readNumber(MAX_LINE_LIMIT) + 1;

  let /** @type{number} */ scale = (_width * _width + _height * _height) * f2 * f2;
  for (let /** @type{number} */ i = 0; i < MAX_LEVEL; ++i) {
    _levelScale[i] = scale / BASE_SCALE_FACTOR;
    scale = ((scale * SCALE_STEP_FACTOR) / f3) | 0;
  }

  let /** @type{number} */ bits = SinCos.MAX_ANGLE_BITS - f1;
  for (let /** @type{number} */ i = 0; i < MAX_LEVEL; ++i) {
    angleBits[i] = math.max(bits - i - (((i * f4) / 2) | 0), 0);
  }
  let /** @type{number} */ colorCode = readNumber(MAX_COLOR_CODE);
  _colorQuant = makeColorQuant(colorCode);
  _paletteSize = colorCode - MAX_COLOR_QUANT_OPTIONS + 1;
};

/**
 * @param{!Int32Array} region
 * @return{number}
 */
export let getLevel = (region) => {
  let /** @type{number} */ min_y = _height + 1;
  let /** @type{number} */ max_y = -1;
  let /** @type{number} */ min_x = _width + 1;
  let /** @type{number} */ max_x = -1;
  forEachScan(region, (y, x0, x1) => {
    min_y = min_y < y ? min_y : y;
    max_y = max_y > y ? max_y : y;
    min_x = min_x < x0 ? min_x : x0;
    max_x = max_x > x1 ? max_x : x1;
  });
  assertFalse(max_y < 0);

  let /** @type{number} */ dx = max_x - min_x;
  let /** @type{number} */ dy = max_y + 1 - min_y;
  let /** @type{number} */ d = dx * dx + dy * dy;
  let /** @type{number} */ i = 0;
  for (; i < MAX_LEVEL - 1; ++i) {
    if (d >= _levelScale[i]) {
      break;
    }
  }
  return i;
};

/**
 * @param{number} v
 * @param{number} q
 * @return{number}
 */
export let dequantizeColor = (v, q) => (255 * v / (q - 1)) | 0;

/** @const @type{number} */
export let MAX_CODE = MAX_PARTITION_CODE * MAX_COLOR_CODE;

/** @const @type{number} */
export let NODE_FILL = 0;
/** @const @type{number} */
export let NODE_HALF_PLANE = 1;
/** @const @type{number} */
export let NODE_TYPE_COUNT = 2;
