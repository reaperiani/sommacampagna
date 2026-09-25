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

inline bool parsePort(const juce::String& text, uint16_t& port) noexcept
{
    const auto value = text.trim();
    if (value.isEmpty())
        return false;

    uint32_t parsed = 0;
    for (int i = 0; i < value.length(); ++i)
    {
        const auto character = value[i];
        if (character < '0' || character > '9')
            return false;

        const auto digit = static_cast<uint32_t>(character - '0');
        if (parsed > 6553u || (parsed == 6553u && digit > 5u))
            return false;
        parsed = parsed * 10u + digit;
    }

    if (parsed == 0u)
        return false;

    port = static_cast<uint16_t>(parsed);
    return true;
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
            parsePort(line.fromFirstOccurrenceOf("=", false, false), p.senderToEngine);
        else if (line.startsWith("engineToReceiver="))
            parsePort(line.fromFirstOccurrenceOf("=", false, false), p.engineToReceiver);
        else if (line.startsWith("transmissionBufferMs="))
            p.transmissionBufferMs = line.fromFirstOccurrenceOf("=", false, false).getFloatValue();
    }

    p.transmissionBufferMs = sanitizeTransmissionBufferMs(p.transmissionBufferMs);

    return p;
}
}
