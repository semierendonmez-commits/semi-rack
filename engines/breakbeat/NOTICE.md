# BREAKBEAT engine — third-party notices

* `upstream/slice_select.c`, `upstream/slice_select.h`: https://github.com/mestela/schwung-breakbeat
  (Matt Estela, Charles Vestal). Upstream `src/module.json` declares MIT; compiled unchanged.
* `upstream/slice_select_n.*`, `BreakbeatEngine.*`: semierendonmez-commits/breakbeat (derived from the
  above, MIT). `slice_select_n.h` lost its `extern "C"` block; the engine lost JUCE (see its header).
* `BBCut.*`: C++ port of BBCut2 cut procedures (Nick Collins, GPL) via the TypeScript port in
  https://github.com/bencodec/BBenCut (no licence file in that repository). SEMI Family is GPL-3.0-or-later.

MIT License (schwung-breakbeat)

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES
OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
