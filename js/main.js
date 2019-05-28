//import * as CjkDecoder from './CjkDecoder.js';
//import * as Decoder from './Decoder.js';

document.addEventListener('DOMContentLoaded', function() {
  //let content = /** @type{!Node} */ (document.getElementById('content'));
  //let input = /** @type {!HTMLInputElement} */ (document.getElementById('encoded'));

  const canvas = /** @type{!HTMLCanvasElement} */ (document.getElementById('canvas'));
  const ctx = /** @type{!CanvasRenderingContext2D} */ (canvas.getContext('2d'));

  /** @param{string} text */
  //function report(text) {
  //  let p = document.createElement('p');
  //  let t = document.createTextNode(text);
  //  p.appendChild(t);
  //  content.appendChild(p);
  //}

  /**
   * @param{string} str
   * @return{number}
   */
  function chr(str) {
    return str.codePointAt(0);
  }

  //function decodeInput() {
  //  let materialized = input.value;
  //  let dematerialized = Array.from(materialized).map(chr);
  //  let decoded = CjkDecoder.decode(dematerialized);
  //  report(decoded.join(', '));
  //}


  let /** @type{string} */cat64 = "VFCGshDtPcnDAnZLeIP05Mnci4+ka/4YnEI7D+CDptw=";
  let cat = new Uint8Array(Array.from(atob(cat64)).map(chr));
  let catIm = /** @type{!ImageData} */ (window['decode2im'](cat, 64, 64));
  ctx.putImageData(catIm, 0, 0);

  //input.addEventListener('input', decodeInput);
});
