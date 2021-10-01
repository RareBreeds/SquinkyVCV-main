#pragma once

#include <string>
#include <vector>

#include "GateTrigger.h"
#include "OneShot.h"
#include "SqLog.h"

class SeqClock2 {
public:
    /**
     * @param resetMetricTime is the time we go to on reset
     */
    SeqClock2(double resetMetricTime);
    SeqClock2();  // dfault ctor, just for test

    enum class ClockRate {
        Div1,   // quarter
        Div2,   // eighth
        Div4,   // sixteenth
        Div8,   // thirty second
        Div16,  // sixty-fourth
        Div32,
        Div64,
        Div96,
        NUM_CLOCKS
    };

    /**
     * Data returned when clock is advanced
     */
    class ClockResults {
    public:
        double totalElapsedTime = 0;
        bool didReset = false;
    };

    /**
     * param samplesElapsed is how many sample clocks have elapsed since last call.
     * param externalClock - is the clock CV, 0..10. will look for rising edges
     * param runStop is the run/stop flag. External logic must toggle it. It is level
     *      sensitive, so clock stays stopped as long as it is low
     * param reset is edge sensitive. Only false -> true transition will trigger a reset.
     *
     * return - total elapsed metric time
     *
     * note that only one of the two passed params will be used, depending 
     * on internal/external model.
     */
    ClockResults update(int samplesElapsed, float externalClock, bool runStop, float reset);

    void setup(ClockRate inputSetting, float, float sampleTime);
    void reset(bool internalClock);

    static std::vector<std::string> getClockRates();

    double getCurMetricTime() const {
        return curMetricTime;
    }

    double getMetricTimePerClock() {
        return metricTimePerClock;
    }

    static int clockRate2Div(ClockRate);

    // void setSampleTime(float);
private:
    ClockRate clockSetting = ClockRate::Div4;

    // TODO: should this also be resetMetricTime?
    double curMetricTime = -1;  // this is correct for external, who cares about internal?
    float sampleTime = 1.f / 44100.f;
    double metricTimePerClock = 1;
    const double resetMetricTime;

    GateTrigger clockProcessor;
    GateTrigger resetProcessor;
    OneShot resetLockout;
};

// We don't want reset logic on clock, as clock high should not be ignoreed.
// Probably don't want on reset either.
inline SeqClock2::SeqClock2(double t) : resetMetricTime(t),
                                        clockProcessor(false),
                                        resetProcessor(false) {
    resetLockout.setDelayMs(1);
    resetLockout.setSampleTime(1.f / 44100.f);
}

inline SeqClock2::SeqClock2() : resetMetricTime(0),
                                clockProcessor(false),
                                resetProcessor(false) {
    resetLockout.setDelayMs(1);
    resetLockout.setSampleTime(1.f / 44100.f);
}
inline SeqClock2::ClockResults SeqClock2::update(int samplesElapsed, float externalClock, bool runStop, float reset) {
    ClockResults results;
    // if stopped, don't do anything

    resetProcessor.go(reset);
    results.didReset = resetProcessor.trigger();
    if (results.didReset) {
        resetLockout.set();

        // go back to start.
        // Fpr old version: For correct start, go negative, so that first clock plays first note.
        // Now caller specifices
        curMetricTime = resetMetricTime;
        // reset the clock so that high clock can gen another clock
        clockProcessor.reset();
    }
    for (int i = 0; i < samplesElapsed; ++i) {
        resetLockout.step();  // TODO: don't iterate
    }

    if (!runStop) {
        results.totalElapsedTime = curMetricTime;
        SQINFO("","leaving on runstop");
        return results;
    }

    // ignore external clock during lockout
    if (resetLockout.hasFired()) {
        // external clock
        clockProcessor.go(externalClock);
        if (clockProcessor.trigger()) {
            SQINFO("","seqClock proc new one");
            // if an external clock fires, advance the time.
            // But if we are reset (negative time), then always go to zero
            if (curMetricTime >= 0) {
                curMetricTime += metricTimePerClock;
                SQINFO("","cur ret adv to %f 1/64=%f", curMetricTime, 1.0 / 64);
            } else {
                curMetricTime = 0;
                SQINFO("","curMetricTime set to zero");
            }
        }
    }

    results.totalElapsedTime = curMetricTime;
    return results;
}

inline void SeqClock2::reset(bool internalClock) {
#ifdef _LOGX
    printf("SeqCLock::reeset\n");
#endif
    assert(false);  // is this used now?
    curMetricTime = internalClock ? 0 : resetMetricTime;
}

inline int SeqClock2::clockRate2Div(ClockRate r) {
    int ret = 1;
    switch (r) {
        case ClockRate::Div96:
            ret = 96;
            break;
        case ClockRate::Div64:
            ret = 64;
            break;
        case ClockRate::Div32:
            ret = 32;
            break;
        case ClockRate::Div16:
            ret = 16;
            break;
        case ClockRate::Div8:
            ret = 8;
            break;
        case ClockRate::Div4:
            ret = 4;
            break;
        case ClockRate::Div2:
            ret = 2;
            break;
        case ClockRate::Div1:
            ret = 1;
            break;
        default:
            assert(false);
    }
    return ret;
}

inline void SeqClock2::setup(ClockRate inputSetting, float, float sampleT) {
    sampleTime = sampleT;
    clockSetting = inputSetting;
    SQINFO("","setting = %d, 96=%d", clockSetting, ClockRate::Div96);
    resetLockout.setSampleTime(sampleT);
    switch (clockSetting) {
        case ClockRate::Div96:
            metricTimePerClock = 1.0 / 96.0;
            break;
        case ClockRate::Div64:
            SQINFO("","clock setup div64");
            metricTimePerClock = .0625 / 4.0;
            break;
        case ClockRate::Div32:
            metricTimePerClock = .0625 / 2.0;
            break;
        case ClockRate::Div16:
            metricTimePerClock = .0625;
            break;
        case ClockRate::Div8:
            metricTimePerClock = .125;
            break;
        case ClockRate::Div4:
            metricTimePerClock = .25;
            break;
        case ClockRate::Div2:
            metricTimePerClock = .5;
            break;
        case ClockRate::Div1:
            metricTimePerClock = 1;
            break;
        default:
            printf("setup clock, reat = %d\n", int(inputSetting));
            fflush(stdout);
            assert(false);
    }
    SQINFO("","after clock setup, met/ck = %f", metricTimePerClock);
}

inline std::vector<std::string> SeqClock2::getClockRates() {
    return {
        "x1 Quarter",
        "x2 8th",
        "x4 16th",
        "x8 32nd",
        "x16 64th",
        "x32",
        "x64",
        "x96"
    };
}
