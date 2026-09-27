#pragma once

#include <cmath>
#include <cstdint>
#include <optional>

#include <juce_core/juce_core.h>

#include "Protocol.h"

namespace somma
{
struct UdpPorts
{
    uint16_t senderToEngine = senderToEnginePort;
    uint16_t engineToReceiver = engineToReceiverPort;
    float transmissionBufferMs = defaultTransmissionBufferMs;
    bool hasTransmissionBufferMs = false;
    std::optional<float> totalTransmissionBufferMs;
};

inline float sanitizeTransmissionBufferMs(float value) noexcept
{
    if (!std::isfinite(value))
        return defaultTransmissionBufferMs;
    if (value < minTransmissionBufferMs)
        return minTransmissionBufferMs;
    if (value > maxTransmissionBufferMs)
        return maxTransmissionBufferMs;
    return value;
}

inline float sanitizeTotalTransmissionBufferMs(float value) noexcept
{
    if (!std::isfinite(value))
        return defaultTotalTransmissionBufferMs;
    if (value < minTotalTransmissionBufferMs)
        return minTotalTransmissionBufferMs;
    if (value > maxTotalTransmissionBufferMs)
        return maxTotalTransmissionBufferMs;
    return value;
}

inline float getEngineTransmissionBufferMs(float totalBufferMs) noexcept
{
    const auto total = sanitizeTotalTransmissionBufferMs(totalBufferMs);
    return juce::jmin(maxTransmissionBufferMs, total * engineTransmissionBufferShare);
}

inline float getReceiverTransmissionBufferMs(float totalBufferMs) noexcept
{
    const auto total = sanitizeTotalTransmissionBufferMs(totalBufferMs);
    return sanitizeTransmissionBufferMs(total - getEngineTransmissionBufferMs(total));
}

inline float migrateLegacyTransmissionBufferMs(float legacyBufferMs) noexcept
{
    return sanitizeTotalTransmissionBufferMs(sanitizeTransmissionBufferMs(legacyBufferMs) * 2.0f);
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
    juce::String text = "senderToEngine=" + juce::String((int) p.senderToEngine)
                      + "\nengineToReceiver=" + juce::String((int) p.engineToReceiver)
                      + "\ntransmissionBufferMs=" + juce::String(sanitizeTransmissionBufferMs(p.transmissionBufferMs), 1) + "\n";
    if (p.totalTransmissionBufferMs.has_value())
        text += "totalTransmissionBufferMs=" + juce::String(sanitizeTotalTransmissionBufferMs(*p.totalTransmissionBufferMs), 1) + "\n";
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

inline bool parseBufferMs(const juce::String& text, float& bufferMs) noexcept
{
    const auto value = text.trim();
    if (value.isEmpty())
        return false;

    double parsed = 0.0;
    double fractionalScale = 0.0;
    bool haveDigit = false;
    bool haveDecimalPoint = false;
    for (int i = 0; i < value.length(); ++i)
    {
        const auto character = value[i];
        if (character >= '0' && character <= '9')
        {
            haveDigit = true;
            const auto digit = static_cast<double>(character - '0');
            if (haveDecimalPoint)
            {
                fractionalScale *= 0.1;
                parsed += digit * fractionalScale;
            }
            else
            {
                parsed = parsed * 10.0 + digit;
            }

            if (!std::isfinite(parsed))
                return false;
        }
        else if (character == '.' && !haveDecimalPoint)
        {
            haveDecimalPoint = true;
            fractionalScale = 1.0;
        }
        else
        {
            return false;
        }
    }

    if (!haveDigit)
        return false;

    const auto converted = static_cast<float>(parsed);
    if (!std::isfinite(converted))
        return false;

    bufferMs = converted;
    return true;
}

inline UdpPorts parsePortsText(const juce::String& text)
{
    UdpPorts p;
    for (const auto& line : juce::StringArray::fromLines(text))
    {
        if (line.startsWith("senderToEngine="))
            parsePort(line.fromFirstOccurrenceOf("=", false, false), p.senderToEngine);
        else if (line.startsWith("engineToReceiver="))
            parsePort(line.fromFirstOccurrenceOf("=", false, false), p.engineToReceiver);
        else if (line.startsWith("transmissionBufferMs="))
        {
            float parsed = 0.0f;
            if (parseBufferMs(line.fromFirstOccurrenceOf("=", false, false), parsed) && parsed > 0.0f)
            {
                p.transmissionBufferMs = sanitizeTransmissionBufferMs(parsed);
                p.hasTransmissionBufferMs = true;
            }
        }
        else if (line.startsWith("totalTransmissionBufferMs="))
        {
            float parsed = 0.0f;
            if (parseBufferMs(line.fromFirstOccurrenceOf("=", false, false), parsed) && parsed > 0.0f)
                p.totalTransmissionBufferMs = sanitizeTotalTransmissionBufferMs(parsed);
        }
    }

    return p;
}

inline UdpPorts readPorts()
{
    const auto file = getPortsFile();
    if (!file.existsAsFile())
        return {};

    return parsePortsText(file.loadFileAsString());
}
}
