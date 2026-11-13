// nam_bridge.h - the only door between Monolith and the Neural Amp Modeler engine (NAM Core, MIT).
// Kept free of NAM / Eigen headers so the plugin file compiles quickly; implemented in nam_engine.cpp.

#ifndef MONOLITH_NAM_BRIDGE_H
#define MONOLITH_NAM_BRIDGE_H

#include <string>

namespace rig {

struct NamModel;   // a loaded capture plus, when needed, the resamplers around it

// Load a .nam capture. hostRate is the MPC's sample rate; with hiFi the model runs at the rate it
// was trained at (normally 48 kHz) between two resamplers, otherwise it is fed the host rate as is.
// Allocates and reads a file: worker thread or prepare() only. Returns nullptr and fills `error`.
NamModel* namLoad(const std::string& path, double hostRate, bool hiFi, std::string& error);
void namFree(NamModel* m);

// Realtime safe.
void namProcess(NamModel* m, float* inout, int numFrames);   // mono, in place
void namSetFull(NamModel* m, bool full);                     // A2 captures: Full (8 channels) or Lite (3)
bool namIsSlimmable(const NamModel* m);
int  namLatency(const NamModel* m);                          // samples at the host rate

} // namespace rig

#endif
