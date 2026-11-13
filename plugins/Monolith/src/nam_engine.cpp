// nam_engine.cpp - builds Neural Amp Modeler Core into the plugin and wraps it for Monolith.
//
// NAM Core (https://github.com/sdatkinson/NeuralAmpModelerCore, MIT, (c) Steven Atkinson) is in
// src/NAM/, unchanged. The SDK build only compiles src/*.cpp, so its sources are included here as
// one translation unit. It needs Eigen (src/Eigen, MPL-2.0) and nlohmann/json (src/json.hpp, MIT).
// NAM_ENABLE_A2_FAST selects the hand-optimised path for A2 captures, which is what makes them
// affordable on the MPC's CPU.

#define NAM_SAMPLE_FLOAT
#define NAM_ENABLE_A2_FAST

#if defined(__clang__)
  #pragma clang diagnostic ignored "-Wunused-parameter"
  #pragma clang diagnostic ignored "-Wsign-compare"
  #pragma clang diagnostic ignored "-Wunused-variable"
  #pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

#include "NAM/activations.cpp"
#include "NAM/container.cpp"
#include "NAM/conv1d.cpp"
#include "NAM/convnet.cpp"
#include "NAM/dsp.cpp"
#include "NAM/get_dsp.cpp"
#include "NAM/linear.cpp"
#include "NAM/lstm.cpp"
#include "NAM/nam_file.cpp"
#include "NAM/ring_buffer.cpp"
#include "NAM/sequential.cpp"
#include "NAM/util.cpp"
#include "NAM/wav.cpp"
#include "NAM/wavenet/a2_fast.cpp"
#include "NAM/wavenet/model.cpp"
#include "NAM/wavenet/slimmable.cpp"

#include "NAM/slimmable.h"

#include <memory>

#include "mpc_resources.h"
#include "nam_bridge.h"
#include "rig_dsp.h"

namespace rig {

namespace {
constexpr int kChunk = 128;                 // host frames per pass through the model
constexpr int kMid = 512;                   // room for a chunk at the model's rate
constexpr int kFifo = 1024;                 // power of two
}

struct NamModel
{
    std::unique_ptr<nam::DSP> dsp;
    nam::SlimmableModel* slim = nullptr;
    bool resample = false;
    Resampler up, down;
    float gain = 1.0f;                      // loudness normalisation
    int latency = 0;
    float midIn[kMid], midOut[kMid], back[kChunk + 16];
    float fifo[kFifo];
    int fifoRead = 0, fifoWrite = 0;
};

NamModel* namLoad(const std::string& path, double hostRate, bool hiFi, std::string& error)
{
    std::unique_ptr<NamModel> m(new NamModel());
    try
    {
        std::string text;
        if (!mpc::readTextFile(path, text)) { error = "cannot read file"; return nullptr; }
        const nlohmann::json j = nlohmann::json::parse(text);
        m->dsp = nam::get_dsp(j);
        if (!m->dsp) { error = "not a model"; return nullptr; }
        if (m->dsp->NumInputChannels() != 1 || m->dsp->NumOutputChannels() != 1) { error = "not a mono model"; return nullptr; }
        m->slim = dynamic_cast<nam::SlimmableModel*>(m->dsp.get());
        if (m->slim) m->slim->SetSlimmableSize(0.0);          // start on the lightest size

        double modelRate = m->dsp->GetExpectedSampleRate();
        if (modelRate <= 0.0) modelRate = 48000.0;            // old files do not say; 48 kHz is the NAM standard
        m->resample = hiFi && std::fabs(modelRate - hostRate) > 1.0
                      && m->up.init(hostRate, modelRate, kChunk) && m->down.init(modelRate, hostRate, kMid);
        m->dsp->Reset(modelRate, kMid);                       // allocates and pre-warms

        if (m->dsp->HasLoudness())                            // bring every capture to about the same level
            m->gain = std::pow(10.0f, std::max(-24.0f, std::min(24.0f, (float)(-22.0 - m->dsp->GetLoudness()))) / 20.0f);

        if (m->resample)
        {
            // The two resamplers hand back N-1, N or N+1 samples for N in. Find how far the output can
            // fall behind and start the output FIFO with that many zeros: constant latency, no underrun.
            Resampler a = m->up, b = m->down;
            long in = 0, out = 0, worst = 0;
            for (int i = 0; i < 147 * 160 / 4; ++i) { ++in; out += b.count(a.count(1)); worst = std::max(worst, in - out); }
            m->latency = (int)worst + 1;
            std::memset(m->fifo, 0, sizeof(m->fifo));
            m->fifoWrite = m->latency;
        }
    }
    catch (const std::exception& e) { error = e.what(); return nullptr; }
    catch (...) { error = "unknown error"; return nullptr; }
    return m.release();
}

void namFree(NamModel* m) { delete m; }
bool namIsSlimmable(const NamModel* m) { return m != nullptr && m->slim != nullptr; }
int namLatency(const NamModel* m) { return m != nullptr ? m->latency : 0; }
void namSetFull(NamModel* m, bool full) { if (m != nullptr && m->slim != nullptr) m->slim->SetSlimmableSize(full ? 1.0 : 0.0); }

void namProcess(NamModel* m, float* x, int numFrames)
{
    if (m == nullptr || !m->dsp) return;
    for (int done = 0; done < numFrames; done += kChunk)
    {
        const int n = std::min(kChunk, numFrames - done);
        float* io = x + done;
        if (!m->resample)
        {
            float* in = io; float* out = m->midOut;
            m->dsp->process(&in, &out, n);
            for (int i = 0; i < n; ++i) io[i] = out[i] * m->gain;
            continue;
        }
        const int mid = m->up.process(io, n, m->midIn);
        if (mid > 0)
        {
            float* in = m->midIn; float* out = m->midOut;
            m->dsp->process(&in, &out, mid);
            const int got = m->down.process(m->midOut, mid, m->back);
            for (int i = 0; i < got; ++i) { m->fifo[m->fifoWrite] = m->back[i]; m->fifoWrite = (m->fifoWrite + 1) & (kFifo - 1); }
        }
        for (int i = 0; i < n; ++i)
        {
            if (m->fifoRead == m->fifoWrite) { io[i] = 0.0f; continue; }     // cannot happen; stay safe
            io[i] = m->fifo[m->fifoRead] * m->gain;
            m->fifoRead = (m->fifoRead + 1) & (kFifo - 1);
        }
    }
}

} // namespace rig
