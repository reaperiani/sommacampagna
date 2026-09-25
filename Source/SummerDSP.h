#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

struct HotSummerParams
{
    int consoleFlavor = 0;
    float compensatedDriveDb = 0.0f;
    int powerSupplyType = 0;
    float masterOutputDb = 0.0f;
    float gravityPct = 0.0f;
    bool bypass = false;
};

class HotSummerDSP
{
public:
    void prepare(double sampleRate)
    {
        sr = sampleRate;
        invSr = sr > 0.0 ? 1.0 / sr : 1.0;
        halfPi = 1.57079632679;
        log2Inv = 1.0 / std::log(2.0);
        dithConst = 1.1 * std::pow(10.0, -44.0);
        denorm = std::pow(10.0, -30.0);
        paramSmooth = std::exp(-invSr / 0.040);
        fadeSmooth = std::exp(-invSr / 0.005);
        c2IirVal = 0.0015 * (48000.0 * invSr);
        nhAtt = std::exp(-1.0 / (sr * 0.1));

        reset();
    }

    void reset()
    {
        railSmooth = 1.0;
        sHeadroom = 1.0;
        lastSumL = 0.0;
        lastSumR = 0.0;
        hHystL = 0.0;
        hHystR = 0.0;
        dcL = 0.0;
        dcR = 0.0;
        sumWdL = sumWwL = sumWdR = sumWwR = 0.0;
        sDrive = 1.0;
        sRecovery = 1.0;
        sIronGain = 0.0;
        sXtalkGain = 0.0;
        sGravity = 0.0;
        fpdL = 16386u;
        fpdR = 16386u;

        for (int i = 0; i < 8; ++i)
        {
            activeMap[i] = 1;
            stemVol[i] = 1.0;
            stemGr[i] = 1.0;
            xtLf1[i] = 0.0;
            xtRf1[i] = 0.0;
        }
    }

    void process(float** in, float* outL, float* outR, int numSamples, const HotSummerParams& p)
    {
        const double targetDrive = std::pow(10.0, p.compensatedDriveDb / 20.0);
        const double headroomOffset = std::pow(10.0, -p.compensatedDriveDb / 60.0);
        const double targetIron = 15.0;
        const double outGain = std::pow(10.0, (p.masterOutputDb - 6.0f) / 20.0f);
        const double targetXtalk = std::pow(10.0, (-95.0 + 10.0 * 0.5) / 20.0);

        int activeStems = 8;
        const double targetHeadroom = activeStems > 1 ? 1.0 / std::pow((double) activeStems, 0.25) : 1.0;

        for (int n = 0; n < numSamples; ++n)
        {
            sDrive = sDrive * paramSmooth + targetDrive * (1.0 - paramSmooth);
            sRecovery = 1.0 / sDrive;
            sIronGain = sIronGain * paramSmooth + (targetIron / 100.0) * (1.0 - paramSmooth);
            sXtalkGain = sXtalkGain * paramSmooth + targetXtalk * (1.0 - paramSmooth);
            sGravity = sGravity * paramSmooth + (juceClamp(p.gravityPct, 0.0f, 100.0f) * 0.01) * (1.0 - paramSmooth);

            double sumL = 0.0;
            double sumR = 0.0;
            double dryL = 0.0;
            double dryR = 0.0;
            double totalDemand = 0.0;

            const double gStressFactor = (1.0 - railSmooth) * 5.0;
            const double gravThresh = 0.6 - (sGravity * 0.4) - (gStressFactor * 0.2 * sGravity);
            const double gravRatio = 1.5 + (sGravity * 8.0) + (gStressFactor * 4.0);
            const double attMs = 30.0 - (sGravity * 29.0);
            const double relMs = 200.0 - (sGravity * 150.0);
            const double grAtt = std::exp(-invSr / (attMs * 0.001));
            const double grRel = std::exp(-invSr / (relMs * 0.001));

            for (int s = 0; s < 8; ++s)
            {
                const int base = s * 2;
                stemVol[s] = stemVol[s] * fadeSmooth + 1.0 * (1.0 - fadeSmooth);
                const double v = stemVol[s];
                double cL = in[base][n] * v;
                double cR = in[base + 1][n] * v;

                dryL += cL;
                dryR += cR;
                totalDemand += (std::abs(cL) + std::abs(cR)) * 0.5;

                if (sGravity > 0.01)
                {
                    const double stemIn = std::max(std::abs(cL), std::abs(cR));
                    const double over = stemIn - gravThresh;
                    double targetGr = 1.0;
                    if (over > 0.0 && stemIn > 0.0)
                    {
                        const double reduced = over / gravRatio;
                        targetGr = (gravThresh + reduced) / stemIn;
                    }

                    double currGr = stemGr[s];
                    if (targetGr < currGr)
                        currGr = targetGr * grAtt + currGr * (1.0 - grAtt);
                    else
                        currGr = targetGr * grRel + currGr * (1.0 - grRel);

                    stemGr[s] = currGr;
                    const double wetL = cL * currGr;
                    const double wetR = cR * currGr;
                    cL = (cL * 0.70) + (wetL * 0.30);
                    cR = (cR * 0.70) + (wetR * 0.30);
                }
                else
                {
                    stemGr[s] = 1.0;
                }

                xtLf1[s] += ((cL - xtLf1[s]) * 0.4) + denorm;
                const double xtLHigh = cL - xtLf1[s];
                xtRf1[s] += ((cR - xtRf1[s]) * 0.4) + denorm;
                const double xtRHigh = cR - xtRf1[s];

                sumL += cL - (xtRHigh * sXtalkGain * 0.5);
                sumR += cR - (xtLHigh * sXtalkGain * 0.5);
            }

            if (p.bypass)
            {
                outL[n] = (float) dryL;
                outR[n] = (float) dryR;
                railSmooth = 1.0;
                continue;
            }

            double targetSag = 1.0;
            double railInertiaCoeff = 0.0;
            if (p.powerSupplyType == 0)
            {
                const double stress = 6.0 / 100.0 * 5.0;
                targetSag = 1.0 / (1.0 + (totalDemand * stress * 0.15));
                railInertiaCoeff = std::exp(-invSr / (250.0 / 1000.0));
            }
            else if (p.powerSupplyType == 1)
            {
                targetSag = 1.0 / (1.0 + (totalDemand * 0.5 * 0.15));
                railInertiaCoeff = 0.9995;
            }
            else if (p.powerSupplyType == 2)
            {
                targetSag = 1.0 / (1.0 + (totalDemand * 0.05 * 0.15));
                railInertiaCoeff = 0.90;
            }

            railSmooth = railSmooth * railInertiaCoeff + targetSag * (1.0 - railInertiaCoeff);
            sHeadroom = sHeadroom * 0.999 + targetHeadroom * 0.001;
            sumL *= sHeadroom;
            sumR *= sHeadroom;

            const double deltaRefL = sumL;
            const double deltaRefR = sumR;

            double procL = std::asin(juceClamp(sumL * sDrive, -0.999, 0.999));
            double procR = std::asin(juceClamp(sumR * sDrive, -0.999, 0.999));

            double slewBase = 0.50;
            if (p.consoleFlavor == 1) slewBase = 0.38;
            if (p.consoleFlavor == 2) slewBase = 0.70;
            if (p.consoleFlavor == 3) slewBase = 0.85;

            double slewMod = slewBase * (1.0 + (1.0 - railSmooth) * 4.5) * (headroomOffset * 0.5 + 0.5);
            slewMod *= 2.2;

            const double limitL = juceClamp(slewMod * railSmooth, 0.01, 0.99);
            const double limitR = juceClamp(slewMod * railSmooth, 0.01, 0.99);
            procL = lastSumL + (procL - lastSumL) * limitL;
            procR = lastSumR + (procR - lastSumR) * limitR;
            lastSumL = procL;
            lastSumR = procR;

            if (p.consoleFlavor == 0)
            {
                procL += (procL * procL * 0.04) + (procL * procL * procL * 0.15);
                procR += (procR * procR * 0.04) + (procR * procR * procR * 0.15);
            }
            else if (p.consoleFlavor == 1)
            {
                procL += procL * procL * 0.08;
                procR += procR * procR * 0.08;
            }
            else if (p.consoleFlavor == 2)
            {
                procL += (procL * procL * 0.05) + (procL * procL * procL * 0.45);
                procR += (procR * procR * 0.05) + (procR * procR * procR * 0.45);
            }

            const double c2Sat = sIronGain * 0.95;
            double c2Scale = (1.0 - c2Sat);
            c2Scale *= c2Scale;
            c2Scale = std::max(c2Scale, 0.001);
            double c2dL = procL / c2Scale;
            double c2dR = procR / c2Scale;
            hHystL = hHystL * (1.0 - c2IirVal) + c2dL * c2IirVal + denorm;
            hHystR = hHystR * (1.0 - c2IirVal) + c2dR * c2IirVal + denorm;
            c2dL = juceClamp(c2dL, -1.57, 1.57);
            c2dR = juceClamp(c2dR, -1.57, 1.57);
            hHystL = juceClamp(hHystL, -1.57, 1.57);
            hHystR = juceClamp(hHystR, -1.57, 1.57);
            procL += (std::sin(c2dL) - std::sin(hHystL)) * c2Scale;
            procR += (std::sin(c2dR) - std::sin(hHystR)) * c2Scale;

            const double rOff = (1.0 - railSmooth) * 0.12;
            double xL = juceClamp(procL * (1.1 + rOff) / (headroomOffset * 0.5 + 0.5), -12.0, 12.0);
            double xR = juceClamp(procR * (1.1 + rOff) / (headroomOffset * 0.5 + 0.5), -12.0, 12.0);
            const double e2L = std::exp(2.0 * xL);
            const double e2R = std::exp(2.0 * xR);
            procL = (e2L - 1.0) / (e2L + 1.0);
            procR = (e2R - 1.0) / (e2R + 1.0);
            procL *= sRecovery;
            procR *= sRecovery;

            const double makeupGain = 1.0 / sHeadroom;
            double outSampleL = procL * makeupGain;
            double outSampleR = procR * makeupGain;

            sumWdL = sumWdL * nhAtt + (outSampleL * deltaRefL) * (1.0 - nhAtt) + denorm;
            sumWwL = sumWwL * nhAtt + (outSampleL * outSampleL) * (1.0 - nhAtt) + denorm;
            sumWdR = sumWdR * nhAtt + (outSampleR * deltaRefR) * (1.0 - nhAtt) + denorm;
            sumWwR = sumWwR * nhAtt + (outSampleR * outSampleR) * (1.0 - nhAtt) + denorm;

            dcL = dcL * 0.9999 + outSampleL * 0.0001 + denorm;
            dcR = dcR * 0.9999 + outSampleR * 0.0001 + denorm;
            outSampleL -= dcL;
            outSampleR -= dcR;

            outSampleL += dither(fpdL, outSampleL);
            outSampleR += dither(fpdR, outSampleR);

            outL[n] = (float) (outSampleL * outGain);
            outR[n] = (float) (outSampleR * outGain);
        }

        busStressPercent.store((float) ((1.0 - railSmooth) * 100.0), std::memory_order_release);
    }

    float getBusStressPercent() const
    {
        return busStressPercent.load(std::memory_order_acquire);
    }

private:
    template <typename T>
    static T juceClamp(T v, T lo, T hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    double dither(uint32_t& fpd, double sample) const
    {
        const double absS = std::abs(sample);
        const double intEx = absS > 0.0 ? std::floor(std::log(absS) * log2Inv) : 0.0;
        fpd = (fpd * 1103515245u + 12345u) & 2147483647u;
        return ((double) fpd / 2147483647.0 - 0.5) * dithConst * std::pow(2.0, intEx + 62.0);
    }

    double sr = 48000.0;
    double invSr = 1.0 / 48000.0;
    double halfPi = 1.57079632679;
    double log2Inv = 1.0;
    double dithConst = 0.0;
    double denorm = 1e-30;
    double paramSmooth = 0.0;
    double fadeSmooth = 0.0;
    double c2IirVal = 0.0015;
    double nhAtt = 0.0;

    std::array<int, 8> activeMap {};
    std::array<double, 8> stemVol {};
    std::array<double, 8> stemGr {};
    std::array<double, 8> xtLf1 {};
    std::array<double, 8> xtRf1 {};

    double railSmooth = 1.0;
    double sHeadroom = 1.0;
    double lastSumL = 0.0;
    double lastSumR = 0.0;
    double hHystL = 0.0;
    double hHystR = 0.0;
    double dcL = 0.0;
    double dcR = 0.0;
    double sumWdL = 0.0;
    double sumWwL = 0.0;
    double sumWdR = 0.0;
    double sumWwR = 0.0;
    double sDrive = 1.0;
    double sRecovery = 1.0;
    double sIronGain = 0.0;
    double sXtalkGain = 0.0;
    double sGravity = 0.0;
    uint32_t fpdL = 16386u;
    uint32_t fpdR = 16386u;

    std::atomic<float> busStressPercent { 0.0f };
};
