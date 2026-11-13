# SPDX-License-Identifier: GPL-3.0-only
# Armada pack: Clipper by 3dSam (isod89) (github.com/isod89/clipper-fm1 at 7bc15a9), GPL-3.0-only
import _fel
_fel.make(globals(), "clipper", "dl_isod89_clipper-fm1/repo", "Clipper by 3dSam (isod89) (github.com/isod89/clipper-fm1 at 7bc15a9)",
          "GPL-3.0-only (the parts under other licences: LICENSING.md)", expose=["HYBRID", "ACID", "STRINGS", "MACRO", "VPS", "TERRAIN", "FMX", "WSEQ", "JUNO", "VECTOR"],
          skip_common=("G_RTYPE",),
          fixes=[(r'\.level = 100, \.name = "LADDER PAD"', ".level = 100, .name = " + _fel.chars("LADDER PAD")),
                 (r'0, 64, 0, 100, "RING BELLS"', "0, 64, 0, 100, " + _fel.chars("RING BELLS"))])
