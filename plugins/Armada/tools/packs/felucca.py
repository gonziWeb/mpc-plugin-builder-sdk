# SPDX-License-Identifier: GPL-3.0-only
# Armada pack: Felucca by Leo Kuroshita (Hugelton Instruments) (github.com/hugelton/Felucca at 6b1115b), GPL-3.0-only
import _fel
_fel.make(globals(), "felucca", "dl_Felucca/Felucca", "Felucca by Leo Kuroshita (Hugelton Instruments) (github.com/hugelton/Felucca at 6b1115b)",
          "GPL-3.0-only (the parts under other licences: LICENSING.md)", expose=["ANALOG", "FM6", "PHASE", "LOFI", "SAMPLE", "VOICE", "TRIO", "WHEEL", "GRAIN", "PHYS", "NOISE", "SLICE", "DRUM"])
