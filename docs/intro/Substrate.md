SUBSTRATE is automatic DnB low end for a stereo bass, ported from the SUBSTRATE plugin. A YIN tracker finds f0, everything below SPLIT x f0 is folded to mono, and the sub is rebuilt as one clean phase-continuous sine that follows the bass's own fundamental. Cuts between resampled edits are found and crossfaded from a Burg LPC extrapolation before they are heard. Latency is fixed at about 60 ms plus 32 samples. The low-end map shows the tracked f0 over the last 4 s, the mono zone and a tick per repaired edit; the phase scope lines up the input's low band against the rebuilt sub.

**Patch it**
- Patch the bass into IN L / R, take L / R, then set SPLIT and SUB. MONITOR cycles MIX / LOW / UPPER / DELTA so you can hear each part.
- Patch V/OCT to take f0 from a cable instead of the tracker, for example NEUROSTACK's F0 output.
- PHASE cycles FREE / FOLLOW / AUTO, REPAIR switches edit repair (SENS sets its sensitivity). The SUB and F0 outs carry the rebuilt sub and the tracked pitch. MAX PITCH and FADE are in the menu.
