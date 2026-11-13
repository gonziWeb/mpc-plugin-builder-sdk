# SPDX-License-Identifier: GPL-3.0-only
# Armada pack: OberVave by zvenson (github.com/zvenson/obervave at a1f660c), GPL-3.0-only
import _obv
_obv.make(globals(), "obervave", "OberVave by zvenson (github.com/zvenson/obervave at a1f660c)", "GPL-3.0-only",
          "dl_zvenson_obervave/repo", "obv", "OBERVAVE",
          slots=[("O1 WAVE", "P_O1_WAVE"), ("O1 PW", "P_O1_PW"), ("O1 LVL", "P_O1_LVL"), ("NOISE", "P_NOISE"),
                 ("O2 WAVE", "P_O2_WAVE"), ("O2 PW", "P_O2_PW"), ("O2 LVL", "P_O2_LVL"), ("O2 SEMI", "P_O2_SEMI"),
                 ("SYNC", "P_SYNC"), ("X-MOD", "P_XMOD"), ("ENV>O2", "P_ENV_O2"), ("O2 FINE", "P_O2_FINE"),
                 ("CUTOFF", "P_CUTOFF"), ("RESO", "P_RESO"), ("F ENV", "P_FENV_AMT"), ("KEY TRK", "P_KEYTRK"),
                 ("F TYPE", "P_FTYPE"), ("MORPH", "P_FMORPH"), ("DRIVE", "P_DRIVE"), ("VEL>FLT", "P_VEL_FLT"),
                 ("F ATK", "P_FA"), ("F DEC", "P_FD"), ("F SUS", "P_FS"), ("F REL", "P_FR"),
                 ("VEL>AMP", "P_VEL_AMP"), ("LFO SHP", "P_LFO_SHAPE"), ("LFO PWM", "P_LFO_PWM"), ("LFO DLY", "P_LFO_DELAY"),
                 ("UNI VOX", "P_UNI_N"), ("VINTAGE", "P_VINTAGE"), ("DLY TONE", "P_DLY_TONE"), ("REV PRE", "P_REV_PRE")],
          common=[("cAtk", "P_AA"), ("cDec", "P_AD"), ("cSus", "P_AS"), ("cRel", "P_AR"), ("cLfoRate", "P_LFO_RATE"),
                  ("cLfoPitch", "P_LFO_PITCH", 1), ("cLfoFilter", "P_LFO_FLT", 1), ("cLfoAmp", "P_LFO_AMP"),
                  ("cVoice", "P_MODE", 2), ("cGlide", "P_GLIDE"), ("cDetune", "P_UNI_DET"), ("cSpread", "P_SPREAD"),
                  ("cChorus", "P_CHO_MIX"), ("cDelay", "P_DLY_MIX"), ("cReverb", "P_REV_MIX"), ("cDlyTime", "P_DLY_TIME", 3),
                  ("cDlyFdbk", "P_DLY_FB"), ("cRevSize", "P_REV_SIZE"), ("cRevDamp", "P_REV_DAMP"),
                  ("cChoRate", "P_CHO_RATE"), ("cChoDepth", "P_CHO_DEPTH")],
          voice_get="x == M_MONO ? 1 : x == M_UNISON ? 3 : 0", voice_set="v == 1 || v == 2 ? M_MONO : v == 3 ? M_UNISON : M_POLY",
          formats=r'''else if (kind == K_WAVE && v >= 0 && v < 4) sg_scpy(out, WAVE[v]);
    else if (kind == K_LSHAPE && v >= 0 && v < 6) sg_scpy(out, LSH[v]);
    else if (kind == K_FTYPE && v >= 0 && v < 6) { static const char *const FT[] = {"LP12", "HP12", "BP12", "NOTCH", "LP24", "SEM"}; sg_scpy(out, FT[v]); }''')
