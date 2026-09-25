#pragma once

#include <cstdint>

#include <juce_core/juce_core.h>

#include "Protocol.h"

namespace somma
{
struct UdpPorts
{
    uint16_t senderToEngine = senderToEnginePort;
    uint16_t engineToReceiver = engineToReceiverPort;
    float transmissionBufferMs = defaultTransmissionBufferMs;
};

inline float sanitizeTransmissionBufferMs(float value) noexcept
{
    if (!(value >= minTransmissionBufferMs))
        return minTransmissionBufferMs;
    if (value > maxTransmissionBufferMs)
        return maxTransmissionBufferMs;
    return value;
}

inline juce::File getPortsFile()
{
    auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                    .getChildFile("sommacampagna");
    if (!base.exists())
        base.createDirectory();
    return base.getChildFile("udp-ports.txt");
}

inline bool writePorts(const UdpPorts& p)
{
    const juce::String text = "senderToEngine=" + juce::String((int) p.senderToEngine)
                            + "\nengineToReceiver=" + juce::String((int) p.engineToReceiver)
                            + "\ntransmissionBufferMs=" + juce::String(sanitizeTransmissionBufferMs(p.transmissionBufferMs), 1) + "\n";
    return getPortsFile().replaceWithText(text);
}

inline UdpPorts readPorts()
{
    UdpPorts p;
    const auto file = getPortsFile();
    if (!file.existsAsFile())
        return p;

    const auto text = file.loadFileAsString();
    for (const auto& line : juce::StringArray::fromLines(text))
    {
        if (line.startsWith("senderToEngine="))
            p.senderToEngine = static_cast<uint16_t>(line.fromFirstOccurrenceOf("=", false, false).getIntValue());
        else if (line.startsWith("engineToReceiver="))
            p.engineToReceiver = static_cast<uint16_t>(line.fromFirstOccurrenceOf("=", false, false).getIntValue());
        else if (line.startsWith("transmissionBufferMs="))
            p.transmissionBufferMs = line.fromFirstOccurrenceOf("=", false, false).getFloatValue();
    }

    if (p.senderToEngine == 0)
        p.senderToEngine = senderToEnginePort;
    if (p.engineToReceiver == 0)
        p.engineToReceiver = engineToReceiverPort;
    p.transmissionBufferMs = sanitizeTransmissionBufferMs(p.transmissionBufferMs);

    return p;
}
}
