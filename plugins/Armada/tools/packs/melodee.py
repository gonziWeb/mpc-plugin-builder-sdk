# SPDX-License-Identifier: GPL-3.0-only
# Armada pack: Melodee by keremimo (github.com/keremimo/melodee at 165d177), GPL-3.0-only
import _fel
_fel.make(globals(), "melodee", "dl_melodee/melodee", "Melodee by keremimo (github.com/keremimo/melodee at 165d177)",
          "GPL-3.0-only (the parts under other licences: LICENSING.md)", expose=["PROPHET", "SID", "DRUM"],
          # Sequential's and Casio's factory banks (packed in Melodee's generated headers) are left out: zeros
          pre_fixes=[(r"static const uint8_t P5_FPACK\[11000\] = \{[^}]*\};", "static const uint8_t P5_FPACK[11000] = {0};   /* (Armada: the factory bank is not shipped) */"),
                     (r"static const uint8_t CZ_LZ\[5115\] = \{[^}]*\};", "static const uint8_t CZ_LZ[5115] = {0};   /* (Armada: the factory tones are not shipped) */")],
          fixes=[(r"uint32_t dest=0;\n(\s*for\(uint32_t i=0;i<1024u;i\+\+\)if\(u->notes\[i\]\.vel&&)", r"uint32_t dest;dest=0;\n\1")],
          # The Prophet's programs here are Gonzi Audio's own, set over Melodee's INIT patch: Sequential's factory bank
          # (200 programs, in Melodee's resources) is not shipped. The CZ-1 engine plays only its factory tones: not offered.
          own_sound=r"""if (e == ENGI_PROPHET) {
        static const uint8_t ARM_P5_0[] = {255};
        static const uint8_t ARM_P5_1[] = {P5_SAW_B, 1, P5_LEVEL_B, 110, P5_FINE_B, 10, P5_CUTOFF, 40, P5_RESONANCE, 10, P5_ENV_FILTER, 80, P5_ATTACK_FILTER, 30, P5_DECAY_FILTER, 70, P5_SUSTAIN_FILTER, 60, P5_RELEASE_FILTER, 40, P5_ATTACK_AMP, 10, P5_DECAY_AMP, 60, P5_RELEASE_AMP, 40, P5_KEY_FILTER, 1, 255};
        static const uint8_t ARM_P5_2[] = {P5_SYNC, 1, P5_FREQ_A, 40, P5_LEVEL_B, 0, P5_POLY_ENV, 95, P5_POLY_FREQ, 1, P5_CUTOFF, 105, P5_DECAY_FILTER, 85, P5_SUSTAIN_FILTER, 0, P5_RELEASE_FILTER, 60, P5_UNISON, 1, P5_UNISON_COUNT, 3, P5_UNISON_DETUNE, 20, P5_GLIDE, 25, 255};
        static const uint8_t ARM_P5_3[] = {P5_PULSE_B, 1, P5_PW_B, 40, P5_FINE_B, 14, P5_LEVEL_A, 110, P5_LEVEL_B, 110, P5_CUTOFF, 50, P5_RESONANCE, 15, P5_ENV_FILTER, 40, P5_ATTACK_FILTER, 80, P5_DECAY_FILTER, 90, P5_SUSTAIN_FILTER, 80, P5_RELEASE_FILTER, 85, P5_ATTACK_AMP, 75, P5_RELEASE_AMP, 85, P5_VINTAGE, 50, P5_KEY_FILTER, 1, 255};
        static const uint8_t ARM_P5_4[] = {P5_SAW_A, 0, P5_PULSE_A, 1, P5_PW_A, 50, P5_FREQ_A, 0, P5_SAW_B, 1, P5_FREQ_B, 0, P5_FINE_B, 6, P5_LEVEL_B, 90, P5_CUTOFF, 28, P5_RESONANCE, 40, P5_ENV_FILTER, 75, P5_DECAY_FILTER, 45, P5_SUSTAIN_FILTER, 20, P5_RELEASE_FILTER, 20, P5_RELEASE_AMP, 20, 255};
        static const uint8_t ARM_P5_5[] = {P5_SAW_B, 1, P5_FINE_B, 20, P5_LEVEL_A, 110, P5_LEVEL_B, 110, P5_CUTOFF, 78, P5_ATTACK_AMP, 60, P5_RELEASE_AMP, 72, P5_ATTACK_FILTER, 60, P5_RELEASE_FILTER, 70, P5_VINTAGE, 60, P5_KEY_FILTER, 1, 255};
        static const uint8_t ARM_P5_6[] = {P5_SAW_A, 0, P5_PULSE_A, 1, P5_TRI_B, 1, P5_FREQ_B, 43, P5_LEVEL_B, 0, P5_POLY_B, 70, P5_POLY_FREQ, 1, P5_CUTOFF, 110, P5_ENV_FILTER, 30, P5_DECAY_FILTER, 80, P5_SUSTAIN_FILTER, 0, P5_RELEASE_FILTER, 80, P5_DECAY_AMP, 90, P5_SUSTAIN_AMP, 0, P5_RELEASE_AMP, 80, P5_KEY_FILTER, 2, 255};
        static const uint8_t ARM_P5_7[] = {P5_CUTOFF, 20, P5_RESONANCE, 100, P5_ENV_FILTER, 110, P5_ATTACK_FILTER, 90, P5_DECAY_FILTER, 100, P5_SUSTAIN_FILTER, 30, P5_RELEASE_FILTER, 70, P5_RELEASE_AMP, 60, 255};
        static const uint8_t ARM_P5_8[] = {P5_SAW_B, 1, P5_LEVEL_B, 127, P5_FINE_B, 8, P5_UNISON, 1, P5_UNISON_COUNT, 5, P5_UNISON_DETUNE, 40, P5_GLIDE, 25, P5_CUTOFF, 85, P5_RESONANCE, 20, P5_ENV_FILTER, 40, P5_DECAY_FILTER, 70, P5_SUSTAIN_FILTER, 60, 255};
        static const uint8_t ARM_P5_9[] = {P5_LEVEL_A, 0, P5_NOISE, 127, P5_CUTOFF, 60, P5_RESONANCE, 95, P5_KEY_FILTER, 2, P5_ATTACK_AMP, 70, P5_RELEASE_AMP, 80, 255};
        static const uint8_t ARM_P5_10[] = {P5_SAW_A, 0, P5_PULSE_A, 1, P5_PW_A, 20, P5_PULSE_B, 1, P5_PW_B, 30, P5_FREQ_B, 48, P5_LEVEL_B, 90, P5_CUTOFF, 70, P5_RESONANCE, 30, P5_ENV_FILTER, 60, P5_DECAY_FILTER, 40, P5_SUSTAIN_FILTER, 0, P5_RELEASE_FILTER, 30, P5_DECAY_AMP, 60, P5_SUSTAIN_AMP, 0, P5_RELEASE_AMP, 30, P5_KEY_FILTER, 1, 255};
        static const uint8_t ARM_P5_11[] = {P5_SAW_B, 1, P5_FREQ_B, 38, P5_LEVEL_B, 100, P5_CUTOFF, 60, P5_ENV_FILTER, 30, P5_ATTACK_FILTER, 60, P5_ATTACK_AMP, 60, P5_RELEASE_FILTER, 80, P5_RELEASE_AMP, 80, P5_VINTAGE, 40, P5_KEY_FILTER, 1, 255};
        static const uint8_t *const ARM_P5[] = {ARM_P5_0, ARM_P5_1, ARM_P5_2, ARM_P5_3, ARM_P5_4, ARM_P5_5, ARM_P5_6, ARM_P5_7, ARM_P5_8, ARM_P5_9, ARM_P5_10, ARM_P5_11};
        const uint8_t *q = ARM_P5[s % 12];
        p5_patch_t *pp;
        host_preset(&trk[0], e, 0);
        pp = p5_patch_of(&trk[0]);
        p5_patch_init(pp);
        while (q[0] != 255u) { pp->raw[q[0]] = q[1]; q += 2; }
        p5_track_accept(&trk[0]);
        return;
    }""",
          own_name=r"""if (e == ENGI_PROPHET) {
        static const char *const N[] = {"INIT", "BRASS SECTION", "SYNC LEAD", "WARM PAD", "PULSE BASS", "STRINGS", "POLYMOD BELL", "RESO SWEEP", "UNISON LEAD", "NOISE WIND", "CLAV", "FIFTHS PAD"};
        *count = 12;
        return N[s % 12];
    }""")
