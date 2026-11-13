# SPDX-License-Identifier: GPL-3.0-only
# Armada pack: Shine On by zvenson (github.com/zvenson/shineon at e2cb631), GPL-3.0-only
import _obv
_obv.make(globals(), "shineon", "Shine On by zvenson (github.com/zvenson/shineon at e2cb631)", "GPL-3.0-only",
          "dl_zvenson_shineon/repo", "shn", "SHINE ON",
          slots=[("C.BASS", "P_CBASS"), ("CELLO", "P_CELLO"), ("VIOLA", "P_VIOLA"), ("VIOLIN", "P_VIOLIN"),
                 ("TRUMPET", "P_TRUMPET"), ("HORN", "P_HORN"), ("SPLIT", "P_SPLIT"), ("OCTAVE", "P_OCTAVE"),
                 ("TONE", "P_TONE"), ("WARMTH", "P_WARMTH"), ("BRIGHT", "P_BRIGHT"), ("VELOCITY", "P_VEL"),
                 ("ENSEMBLE", "P_ENS_MODE"), ("ENS DEP", "P_ENS_DEPTH"), ("ENS RATE", "P_ENS_RATE"), ("ENS MIX", "P_ENS_MIX"),
                 ("PHA RATE", "P_PHA_RATE"), ("PHA DEP", "P_PHA_DEPTH"), ("PHA FB", "P_PHA_FB"), ("PHA MIX", "P_PHA_MIX"),
                 ("VIB DLY", "P_LFO_DELAY"), ("VINTAGE", "P_VINTAGE"), ("DLY TONE", "P_DLY_TONE"), ("REV PRE", "P_REV_PRE")],
          common=[("cAtk", "P_ATTACK"), ("cRel", "P_RELEASE"), ("cLfoRate", "P_LFO_RATE"), ("cLfoPitch", "P_VIB", 1),
                  ("cGlide", "P_GLIDE"), ("cDelay", "P_DLY_MIX"), ("cReverb", "P_REV_MIX"), ("cDlyTime", "P_DLY_TIME", 3),
                  ("cDlyFdbk", "P_DLY_FB"), ("cRevSize", "P_REV_SIZE"), ("cRevDamp", "P_REV_DAMP")],
          formats=r'''else if (kind == K_ENS && v >= 0 && v < 4) { static const char *const EN[] = {"OFF", "MONO", "STEREO", "WIDE"}; sg_scpy(out, EN[v]); }
    else if (kind == K_OCTS) sg_itoa(v - 1, out);''')
