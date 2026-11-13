# Synthetic guitar-cabinet impulse responses for Monolith (Gonzi Audio, MIT / CC0 - no recording involved).
# A cabinet is drawn as a magnitude curve (low resonance, mid dip, presence peak, steep top roll-off,
# cone break-up ripple), turned into a minimum-phase impulse response and written as 24-bit 48 kHz WAV.
import sys, numpy as np, wave, struct
SR, N = 48000, 16384
f = np.fft.rfftfreq(N, 1 / SR); f[0] = 1e-3
def peak(f0, q, db): return db / (1 + ((f / f0 - f0 / f) * q) ** 2)
def hp(f0, order): return -10 * np.log10(1 + (f0 / f) ** (2 * order))
def lp(f0, order): return -10 * np.log10(1 + (f / f0) ** (2 * order))
def make(name, res, resdb, dip, dipdb, pres, presdb, top, order, ripple, seed):
    rng = np.random.default_rng(seed)
    db = hp(res * 0.8, 2) + peak(res, 2.5, resdb) + peak(dip, 0.9, dipdb) + peak(pres, 1.6, presdb) + lp(top, order) + lp(top * 2.2, 2)
    for _ in range(14):                                   # cone break-up: narrow peaks and notches from 1 kHz up
        db += peak(rng.uniform(900, top * 1.3), rng.uniform(6, 14), rng.uniform(-ripple, ripple * 0.7))
    mag = 10 ** (db / 20)
    cep = np.fft.irfft(np.log(np.maximum(mag, 1e-6)))      # minimum phase through the real cepstrum
    cep[1:N // 2] *= 2; cep[N // 2 + 1:] = 0
    h = np.fft.irfft(np.exp(np.fft.rfft(cep)))[:4096]
    h *= np.concatenate([np.ones(3072), np.hanning(2048)[1024:]])
    h /= np.max(np.abs(h)) * 1.02
    w = wave.open(name, 'wb'); w.setnchannels(1); w.setsampwidth(3); w.setframerate(SR)
    w.writeframes(b''.join(struct.pack('<i', int(round(v * 8388607)))[:3] for v in h)); w.close()
out = sys.argv[1]
make(out + '/Gonzi 412 Dark.wav',   105, 5.0, 420, -3.5, 2600, 5.0, 4300, 4, 4.0, 7)
make(out + '/Gonzi 412 Bright.wav', 115, 4.0, 500, -4.5, 3400, 7.0, 5600, 4, 4.5, 11)
make(out + '/Gonzi 112 Open.wav',   135, 3.0, 350, -2.0, 2200, 4.0, 6200, 3, 3.5, 23)
