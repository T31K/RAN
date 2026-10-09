// Canvas text for gdi_web.cpp: metrics, widths and grayscale coverage masks.
addToLibrary({
  $RanGdi: {
    ctx: null,
    size: [0, 0],
    get(w, h) {
      if (!RanGdi.ctx) {
        const c = typeof OffscreenCanvas !== 'undefined' ? new OffscreenCanvas(64, 64) : document.createElement('canvas');
        RanGdi.ctx = c.getContext('2d', { willReadFrequently: true });
      }
      const c = RanGdi.ctx.canvas;
      if (w && (c.width < w || c.height < h)) {
        c.width = Math.max(c.width, w);
        c.height = Math.max(c.height, h);
      }
      return RanGdi.ctx;
    },
    str(ptr, len) {
      let s = '';
      const base = ptr >> 1;
      for (let i = 0; i < len; i += 4096)
        s += String.fromCharCode.apply(null, HEAPU16.subarray(base + i, base + Math.min(len, i + 4096)));
      return s;
    },
  },

  ran_gdi_font_metrics__deps: ['$RanGdi', '$UTF8ToString'],
  ran_gdi_font_metrics: (cssPtr, out) => {
    const ctx = RanGdi.get();
    ctx.font = UTF8ToString(cssPtr);
    const m = ctx.measureText('가Hgjy');   // a Hangul syllable and Latin ascenders/descenders
    const x = ctx.measureText('x').width;
    const ascent = m.fontBoundingBoxAscent ?? m.actualBoundingBoxAscent;
    const descent = m.fontBoundingBoxDescent ?? m.actualBoundingBoxDescent;
    const wide = ctx.measureText('가W').width;
    HEAPF32[out >> 2] = ascent;
    HEAPF32[(out >> 2) + 1] = descent;
    HEAPF32[(out >> 2) + 2] = x;
    HEAPF32[(out >> 2) + 3] = wide;
  },

  ran_gdi_measure__deps: ['$RanGdi', '$UTF8ToString'],
  ran_gdi_measure: (cssPtr, textPtr, len) => {
    const ctx = RanGdi.get();
    ctx.font = UTF8ToString(cssPtr);
    return ctx.measureText(RanGdi.str(textPtr, len)).width;
  },

  ran_gdi_draw__deps: ['$RanGdi', '$UTF8ToString'],
  ran_gdi_draw: (cssPtr, textPtr, len, w, h, x, baseline, mask) => {
    const ctx = RanGdi.get(w, h);
    ctx.clearRect(0, 0, w, h);
    ctx.fillStyle = '#000';
    ctx.fillRect(0, 0, w, h);
    ctx.font = UTF8ToString(cssPtr);
    ctx.textBaseline = 'alphabetic';
    ctx.fillStyle = '#fff';
    ctx.fillText(RanGdi.str(textPtr, len), x, baseline);
    const px = ctx.getImageData(0, 0, w, h).data;
    for (let i = 0, n = w * h; i < n; i++) HEAPU8[mask + i] = px[i * 4];
  },
});
