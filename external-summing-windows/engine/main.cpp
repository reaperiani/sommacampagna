#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <unordered_map>

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include "Protocol.h"
#include "PortDiscovery.h"
#include "SummerDSP.h"

namespace
{
constexpr int numPairs = 8;
constexpr int numInputChannels = 16;
constexpr uint16_t fixedEngineBlockSamples = 512;
constexpr uint32_t streamTimeoutMs = 1200;

struct StreamState
{
    uint16_t pairIndex = 0;
    uint16_t numSamples = 0;
    uint32_t sampleRate = 48000;
    uint32_t sessionId = 1;
    uint32_t lastSeenMs = 0;
    std::array<float, somma::maxBufferedFrames> ringL {};
    std::array<float, somma::maxBufferedFrames> ringR {};
    size_t writePos = 0;
    size_t readPos = 0;
    size_t availableFrames = 0;
    bool playbackPrimed = false;

    void pushFrame(float l, float r) noexcept
    {
        ringL[writePos] = l;
        ringR[writePos] = r;
        writePos = (writePos + 1u) % somma::maxBufferedFrames;

        if (availableFrames < somma::maxBufferedFrames)
            ++availableFrames;
        else
            readPos = (readPos + 1u) % somma::maxBufferedFrames;
    }

    bool popFrame(float& l, float& r) noexcept
    {
        if (availableFrames == 0)
            return false;

        l = ringL[readPos];
        r = ringR[readPos];
        readPos = (readPos + 1u) % somma::maxBufferedFrames;
        --availableFrames;
        return true;
    }
};

struct EngineSharedState
{
    std::atomic<int> consoleFlavor { 0 };
    std::atomic<float> driveDb { 0.0f };
    std::atomic<int> powerSupplyType { 0 };
    std::atomic<float> outputDb { 0.0f };
    std::atomic<float> gravityPct { 0.0f };
    std::atomic<float> transmissionBufferMs { somma::defaultTransmissionBufferMs };
    std::atomic<bool> bypass { false };

    std::array<std::atomic<float>, numInputChannels> inputMetersDb;
    std::atomic<float> sagPct { 0.0f };

    EngineSharedState()
    {
        for (auto& m : inputMetersDb)
            m.store(-100.0f, std::memory_order_relaxed);
    }
};

uint32_t nowMs() { return juce::Time::getMillisecondCounter(); }

size_t getTargetBufferFrames(float bufferMs, uint32_t sampleRate) noexcept
{
    const auto safeBufferMs = somma::sanitizeTransmissionBufferMs(bufferMs);
    const auto safeSampleRate = sampleRate > 0 ? sampleRate : 48000u;
    const auto frames = static_cast<size_t>((static_cast<double>(safeBufferMs) * static_cast<double>(safeSampleRate)) / 1000.0);
    return juce::jlimit(static_cast<size_t>(fixedEngineBlockSamples), somma::maxBufferedFrames - static_cast<size_t>(fixedEngineBlockSamples), frames);
}
}

class EngineThread final : public juce::Thread
{
public:
    explicit EngineThread(EngineSharedState& s)
        : juce::Thread("SommaEngineThread"), shared(s)
    {
    }

    void run() override
    {
        juce::DatagramSocket inSocket;
        const auto selectedInPort = findAvailablePort(inSocket, somma::senderToEnginePort);
        inSocket.waitUntilReady(false, 0);

        juce::DatagramSocket outSocket;
        const auto selectedOutPort = findAvailablePortByProbe(somma::engineToReceiverPort);
        somma::writePorts({ selectedInPort, selectedOutPort, shared.transmissionBufferMs.load(std::memory_order_relaxed) });

        std::unordered_map<uint32_t, StreamState> streams;
        std::array<std::array<float, somma::maxSamplesPerPacket>, numInputChannels> mixInputs {};
        std::array<float*, numInputChannels> mixPtrs {};
        std::array<float, somma::maxSamplesPerPacket> outL {};
        std::array<float, somma::maxSamplesPerPacket> outR {};

        HotSummerDSP dsp;
        HotSummerParams params;
        uint32_t sampleRate = 48000;
        dsp.prepare(static_cast<double>(sampleRate));

        uint32_t outBlock = 0;
        uint32_t sessionId = 1;
        auto nextSend = juce::Time::getMillisecondCounterHiRes();
        uint32_t lastConfigWriteMs = 0;

        while (!threadShouldExit())
        {
            for (;;)
            {
                somma::StereoAudioPacket p {};
                const int bytes = inSocket.read(reinterpret_cast<char*>(&p), sizeof(p), false);
                if (bytes < static_cast<int>(sizeof(somma::PacketHeader)))
                    break;

                if (!somma::isValidHeader(p.header)
                    || p.header.packetType != static_cast<uint16_t>(somma::PacketType::senderAudio))
                    continue;

                auto& st = streams[p.header.streamId];
                st.pairIndex = static_cast<uint16_t>(p.header.pairIndex % numPairs);
                st.numSamples = p.header.numSamples;
                st.sampleRate = p.header.sampleRate;
                st.sessionId = p.header.sessionId;
                st.lastSeenMs = nowMs();
                for (int i = 0; i < static_cast<int>(p.header.numSamples); ++i)
                    st.pushFrame(p.interleaved[static_cast<size_t>(i * 2)], p.interleaved[static_cast<size_t>(i * 2 + 1)]);

                sessionId = p.header.sessionId;
                sampleRate = p.header.sampleRate > 0 ? p.header.sampleRate : sampleRate;
            }

            const uint32_t configNow = nowMs();
            if ((configNow - lastConfigWriteMs) > 500u)
            {
                somma::writePorts({ selectedInPort, selectedOutPort, shared.transmissionBufferMs.load(std::memory_order_relaxed) });
                lastConfigWriteMs = configNow;
            }

            params.consoleFlavor = shared.consoleFlavor.load(std::memory_order_relaxed);
            params.compensatedDriveDb = shared.driveDb.load(std::memory_order_relaxed);
            params.powerSupplyType = shared.powerSupplyType.load(std::memory_order_relaxed);
            params.masterOutputDb = shared.outputDb.load(std::memory_order_relaxed);
            params.gravityPct = shared.gravityPct.load(std::memory_order_relaxed);
            params.bypass = shared.bypass.load(std::memory_order_relaxed);

            for (auto& channel : mixInputs)
                channel.fill(0.0f);

            const uint32_t t = nowMs();
            const auto targetBufferFrames = getTargetBufferFrames(shared.transmissionBufferMs.load(std::memory_order_relaxed), sampleRate);
            for (auto it = streams.begin(); it != streams.end();)
            {
                if ((t - it->second.lastSeenMs) > streamTimeoutMs)
                {
                    it = streams.erase(it);
                    continue;
                }

                auto& st = it->second;
                if (st.sessionId == sessionId)
                {
                    const int base = st.pairIndex * 2;
                    if (!st.playbackPrimed && st.availableFrames >= targetBufferFrames)
                        st.playbackPrimed = true;

                    if (st.playbackPrimed)
                    {
                        for (int i = 0; i < fixedEngineBlockSamples; ++i)
                        {
                            float l = 0.0f;
                            float r = 0.0f;
                            if (!st.popFrame(l, r))
                            {
                                st.playbackPrimed = false;
                                break;
                            }

                            mixInputs[(size_t) base][(size_t) i] += l;
                            mixInputs[(size_t) base + 1][(size_t) i] += r;
                        }
                    }
                }

                ++it;
            }

            for (int ch = 0; ch < numInputChannels; ++ch)
            {
                mixPtrs[(size_t) ch] = mixInputs[(size_t) ch].data();
                float peak = 0.0f;
                for (int n = 0; n < fixedEngineBlockSamples; ++n)
                    peak = juce::jmax(peak, std::abs(mixInputs[(size_t) ch][(size_t) n]));
                shared.inputMetersDb[(size_t) ch].store(juce::Decibels::gainToDecibels(peak, -100.0f), std::memory_order_relaxed);
            }

            dsp.process(mixPtrs.data(), outL.data(), outR.data(), fixedEngineBlockSamples, params);
            shared.sagPct.store(dsp.getBusStressPercent(), std::memory_order_relaxed);

            somma::StereoAudioPacket outPacket {};
            outPacket.header.magic = somma::protocolMagic;
            outPacket.header.version = somma::protocolVersion;
            outPacket.header.packetType = static_cast<uint16_t>(somma::PacketType::mainStereoSum);
            outPacket.header.sessionId = sessionId;
            outPacket.header.blockIndex = outBlock++;
            outPacket.header.sampleRate = sampleRate;
            outPacket.header.numSamples = fixedEngineBlockSamples;

            for (int i = 0; i < fixedEngineBlockSamples; ++i)
            {
                outPacket.interleaved[(size_t) (i * 2)] = outL[(size_t) i];
                outPacket.interleaved[(size_t) (i * 2 + 1)] = outR[(size_t) i];
            }

            const int bytesToSend = static_cast<int>(sizeof(somma::PacketHeader) + static_cast<size_t>(fixedEngineBlockSamples * 2) * sizeof(float));
            outSocket.write("127.0.0.1", static_cast<int>(selectedOutPort), reinterpret_cast<const char*>(&outPacket), bytesToSend);

            const double blockMs = (static_cast<double>(fixedEngineBlockSamples) / static_cast<double>(sampleRate)) * 1000.0;
            nextSend += blockMs;
            const double waitMs = nextSend - juce::Time::getMillisecondCounterHiRes();
            if (waitMs > 0.0)
                juce::Thread::sleep(static_cast<int>(waitMs));
            else
                nextSend = juce::Time::getMillisecondCounterHiRes();
        }
    }

private:
    static uint16_t findAvailablePort(juce::DatagramSocket& socket, uint16_t preferred)
    {
        for (int i = 0; i < 200; ++i)
        {
            const auto candidate = static_cast<uint16_t>(preferred + i);
            if (socket.bindToPort((int) candidate, "127.0.0.1"))
                return candidate;
        }
        return preferred;
    }

    static uint16_t findAvailablePortByProbe(uint16_t preferred)
    {
        for (int i = 0; i < 200; ++i)
        {
            const auto candidate = static_cast<uint16_t>(preferred + i);
            juce::DatagramSocket probe;
            if (probe.bindToPort((int) candidate, "127.0.0.1"))
                return candidate;
        }
        return preferred;
    }

    EngineSharedState& shared;
};

class MainComponent final : public juce::Component,
                            private juce::Timer
{
public:
    explicit MainComponent(EngineSharedState& s)
        : shared(s)
    {
        addAndMakeVisible(flavor);
        flavor.addItemList({ "Signature", "Vintage British", "Discrete American", "Modern Ultra-Linear" }, 1);
        flavor.onChange = [this] { shared.consoleFlavor.store(flavor.getSelectedItemIndex(), std::memory_order_relaxed); };
        flavor.setSelectedItemIndex(0);

        configureSlider(drive, -12.0, 18.0, 0.0, [this](double v) { shared.driveDb.store((float) v, std::memory_order_relaxed); });
        configureSlider(output, -12.0, 12.0, 0.0, [this](double v) { shared.outputDb.store((float) v, std::memory_order_relaxed); });
        configureSlider(gravity, 0.0, 100.0, 0.0, [this](double v) { shared.gravityPct.store((float) v, std::memory_order_relaxed); });
        configureSlider(transmissionBuffer, somma::minTransmissionBufferMs, somma::maxTransmissionBufferMs, somma::defaultTransmissionBufferMs,
                        [this](double v) { shared.transmissionBufferMs.store(somma::sanitizeTransmissionBufferMs((float) v), std::memory_order_relaxed); });
        transmissionBuffer.setRange(somma::minTransmissionBufferMs, somma::maxTransmissionBufferMs, 1.0);

        addAndMakeVisible(psu);
        psu.addItemList({ "Custom", "Vintage", "Modern", "Mastering" }, 1);
        psu.onChange = [this] { shared.powerSupplyType.store(psu.getSelectedItemIndex(), std::memory_order_relaxed); };
        psu.setSelectedItemIndex(0);

        addAndMakeVisible(bypass);
        bypass.setButtonText("Bypass");
        bypass.onClick = [this] { shared.bypass.store(bypass.getToggleState(), std::memory_order_relaxed); };

        setSize(640, 520);
        startTimerHz(30);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff111418));
        g.setColour(juce::Colours::white);
        g.drawFittedText("sommacampagna_engine", 10, 8, getWidth() - 20, 24, juce::Justification::centred, 1);
        g.setFont(14.0f);
        g.drawFittedText("Console Flavor", 20, 26, 220, 18, juce::Justification::left, 1);
        g.drawFittedText("Power Supply", 300, 26, 220, 18, juce::Justification::left, 1);
        g.drawFittedText("Drive (dB)", 20, 68, 120, 18, juce::Justification::left, 1);
        g.drawFittedText("Output (dB)", 20, 106, 120, 18, juce::Justification::left, 1);
        g.drawFittedText("Gravity (%)", 20, 144, 120, 18, juce::Justification::left, 1);
        g.drawFittedText("Transmission Buffer (ms)", 20, 182, 220, 18, juce::Justification::left, 1);

        auto meterArea = juce::Rectangle<float>(20.0f, 240.0f, 360.0f, 250.0f);
        const float gap = 8.0f;
        const float w = (meterArea.getWidth() - gap * 3.0f) / 4.0f;
        const float h = (meterArea.getHeight() - gap * 3.0f) / 4.0f;
        for (int r = 0; r < 4; ++r)
        {
            for (int c = 0; c < 4; ++c)
            {
                const int idx = r * 4 + c;
                const auto b = juce::Rectangle<float>(meterArea.getX() + c * (w + gap), meterArea.getY() + r * (h + gap), w, h);
                const float db = meterDb[(size_t) idx];
                juce::Colour col = juce::Colour(0xff1d2227);
                if (db >= -1.0f) col = juce::Colour(0xffff4545);
                else if (db >= -8.0f) col = juce::Colour(0xffffd249);
                else if (db >= -60.0f) col = juce::Colour(0xff6dff72);
                g.setColour(col);
                g.fillRoundedRectangle(b, 6.0f);
            }
        }

        g.setColour(juce::Colours::white);
        g.drawFittedText("BUS STRESS (SAG)", 420, 260, 180, 20, juce::Justification::centred, 1);
        g.setColour(juce::Colour(0xff2a3138));
        g.fillRect(420, 286, 180, 24);
        g.setColour(juce::Colour(0xffff7a45));
        g.fillRect(420, 286, static_cast<int>(1.8f * sagPct), 24);
        g.setColour(juce::Colours::white);
        g.drawRect(420, 286, 180, 24);
        g.drawFittedText(juce::String(sagPct, 1) + " %", 420, 314, 180, 20, juce::Justification::centred, 1);
    }

    void resized() override
    {
        flavor.setBounds(20, 44, 260, 28);
        psu.setBounds(300, 44, 220, 28);
        drive.setBounds(20, 86, 300, 28);
        output.setBounds(20, 124, 300, 28);
        gravity.setBounds(20, 162, 300, 28);
        transmissionBuffer.setBounds(20, 200, 300, 28);
        bypass.setBounds(340, 198, 120, 28);
    }

private:
    void timerCallback() override
    {
        for (int i = 0; i < numInputChannels; ++i)
            meterDb[(size_t) i] = shared.inputMetersDb[(size_t) i].load(std::memory_order_relaxed);
        sagPct = shared.sagPct.load(std::memory_order_relaxed);
        repaint();
    }

    void configureSlider(juce::Slider& s, double min, double max, double value, std::function<void(double)> cb)
    {
        addAndMakeVisible(s);
        s.setRange(min, max, 0.1);
        s.setValue(value);
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
        s.onValueChange = [&, cb] { cb(s.getValue()); };
    }

    EngineSharedState& shared;
    juce::ComboBox flavor;
    juce::ComboBox psu;
    juce::Slider drive;
    juce::Slider output;
    juce::Slider gravity;
    juce::Slider transmissionBuffer;
    juce::ToggleButton bypass;

    std::array<float, numInputChannels> meterDb {};
    float sagPct = 0.0f;
};

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow(juce::String name, EngineSharedState& s)
        : DocumentWindow(name,
                         juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId),
                         DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new MainComponent(s), true);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
};

class SommaEngineApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "sommacampagna_engine"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }

    void initialise(const juce::String&) override
    {
        engineThread = std::make_unique<EngineThread>(shared);
        engineThread->startThread();
        mainWindow = std::make_unique<MainWindow>(getApplicationName(), shared);
    }

    void shutdown() override
    {
        if (engineThread != nullptr)
        {
            engineThread->signalThreadShouldExit();
            engineThread->stopThread(2000);
        }
        mainWindow.reset();
        engineThread.reset();
    }

private:
    EngineSharedState shared;
    std::unique_ptr<EngineThread> engineThread;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(SommaEngineApplication)
