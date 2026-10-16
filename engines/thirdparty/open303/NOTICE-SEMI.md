# Open303 (shared third-party DSP)

Open303 by Robin Schmidt (rs-met.com), MIT (see LICENSE), as shipped in schwung-303 (charlesvestal/schwung-303,
dsp/open303) and used unchanged by the ACID plug-in. Vendored here for SEMI Family; first used by ACID.

Changes for the shared plugin binary (no DSP change):
* GlobalFunctions.h: the helpers upstream leaves global (linToExp, amp2dB, pitchToFreq ...) are wrapped in
  `namespace rosic`.
* rosic_FourierTransformerRadix2.cpp: fft4g.c is included inside `namespace rosic::ooura` instead of at
  global scope.
fft4g.c is not compiled on its own (it is #included by rosic_FourierTransformerRadix2.cpp).
