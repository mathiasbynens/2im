/**
 * @noinline
 * @param{number|!Array} n
 * @return {!Int32Array}
 */
export let newInt32Array = (n) => new Int32Array(n);

/**
 * @noinline
 * @param{!Int32Array} dst
 * @param{!Array} src
 * @param{number} offset
 * @return {void}
 */
export let int32ArraySet = (dst, src, offset) => dst.set(src, offset);

/**
 * @noinline
 * @param{!Array|!Int32Array|!Uint8Array} a
 * @return {number}
 */
export let last = (a) =>  a.length - 1;

export let /** @const */ math = Math;

/**
 * @param{number} n
 * @return {number}
 */
export let pow2 = n => 2 ** n;

export let /** @const @noinline @type{number} */ b8 = pow2(8);
export let /** @const @type{number} */ b32 = b8 ** 4;
export let /** @const @type{number} */ b40 = b8 ** 5;
export let /** @const @type{number} */ b48 = b8 ** 6;

export let /** @type{function(number):number} */ mathFloor = math.floor;
export let /** @type{function(number):number} */ mathRound = math.round;

/**
 * @param{!Int32Array} region
 * @param{function(number, number, number)} callback
 * @return {void}
 */
export let forEachScan = (region, callback) => {
  for (let /** @type{number} */ i = region[last(region)] * 3 - 3; i >= 0; i -= 3) {
    callback(region[i], region[i + 1], region[i + 2]);
  }
};

/**
 * @noinline
 * @param{boolean} b
 * @return {void}
 */
export let assertFalse = b => {if (b) throw 4;};
