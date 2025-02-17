#pragma once
#include <optional>
#include <functional>

namespace nes
{
    class SystemBus;

    static uint8_t dutyCyclePulseTables[4][8] = {
        { 0, 1, 0, 0, 0, 0, 0, 0 }, // 12.5%
        { 0, 1, 1, 0, 0, 0, 0, 0 }, // 25%
        { 0, 1, 1, 1, 1, 0, 0, 0 }, // 50%
        { 1, 0, 0, 1, 1, 1, 1, 1 }  // 25% negated
    };

    static uint8_t triangleStepSequences[32] = { 15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0,
                                                0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15 };

    static uint16_t noisePeriodsTable[16] = { 4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068 };

    // Audio processor included in the 2A03 chip (CPU + APU)
    class APU
    {
    public:
        APU(SystemBus* nesBus);
        ~APU() = default;

        void reset();
        void clock();

        float getOutputAPU() const;

        // Communications with CPU
        uint8_t cpuRead(uint16_t address);
        void    cpuWrite(uint16_t address, uint8_t  data);

    public:
        bool irq = false;
        bool dmcInterrupt = false;

    protected:
        static APU* apuInstance;

    private:
        SystemBus* nes;

        uint64_t elapsedCycles = 0;
        uint64_t elapsedFrameCounterCycles = 0;

        /*struct PulseChannelParameters
        {
            uint8_t dutyCycle : 2;
            uint8_t loopEnvelope_disableLengthCounter : 1;
            uint8_t constantVolumeFlag : 1;
            uint8_t envelopeDividerPeriod : 4;

            struct
            {
                uint8_t enabledFlag : 1;
                uint8_t dividerPeriod : 3;
                uint8_t negateFlag : 1;
                uint8_t shiftCount : 3;
            } sweepUnit;

            uint16_t pulseTimer : 11;
            uint8_t lengthCounterLoad : 5;
        };

        PulseChannelParameters pulse1Parameters;*/


        struct EnvelopeGenerator
        {
            bool startFlag = false;
            uint8_t divider = 0x00;
            uint8_t decayLevelCounter = 0x00;
            bool constantVolumeFlag = false;
            bool loopFlag = false;
            uint8_t envelopeDividerPeriodVolume = 0x00;

            void clock()
            {
                if (!startFlag)
                {
                    if (divider > 0)
                        divider--;

                    if (divider == 0)
                    {
                        divider = envelopeDividerPeriodVolume;

                        if (decayLevelCounter > 0)
                        {
                            decayLevelCounter--;
                        }
                        else if (loopFlag)
                        {
                            decayLevelCounter = 15;
                        }
                    }
                }
                else
                {
                    startFlag = false;
                    decayLevelCounter = 15;
                    divider = envelopeDividerPeriodVolume;
                }
            }

            uint8_t output() const
            {
                if (constantVolumeFlag)
                    return envelopeDividerPeriodVolume;

                return decayLevelCounter;
            }
        };

        EnvelopeGenerator pulse1Envelope;
        EnvelopeGenerator pulse2Envelope;
        EnvelopeGenerator noiseEnvelope;

        enum PulseIdentifier
        {
            PULSE1, PULSE2
        };

        struct PulseSequencer
        {
            bool enabled = false;
            uint8_t dutyCycleTable = 0x00;
            //uint8_t sequence;
            uint16_t pulseTimer = 0x0000;
            uint16_t timerReload = 0x0000;
            uint8_t lengthCounterLoad = 0; // Copy of lengthCounter load value
            uint8_t pulseChannelOutput = 0x00;
            uint8_t tableIndex = 0;
            PulseIdentifier pulseId;

            struct SweepUnit
            {
                bool enabled = false;
                uint8_t divider = 0x00;
                uint8_t dividerPeriodReload = 0x00;
                bool reloadFlag = false;
                bool negateFlag = false;
                uint8_t shiftCount = 0x00;
                uint16_t targetPeriod = 0x0000;
                uint16_t changeAmount = 0x0000;
                bool muted = false;
                
                SweepUnit() = delete;

                SweepUnit(PulseSequencer& channel)
                    : pulseChannel(channel) { }

                void clock()
                {
                    if (divider == 0 && this->enabled && !muted)
                    {
                        pulseChannel.timerReload = targetPeriod;
                    }

                    if (divider == 0 || reloadFlag)
                    {
                        divider = dividerPeriodReload;
                        reloadFlag = false;
                    }
                    else
                    {
                        divider--;
                    }

                    //muted = (pulseChannel.timerReload < 8) || (pulseChannel.timerReload > 0x07FF);
                }

                void calculateTargetPeriod()
                {
                    changeAmount = pulseChannel.timerReload >> shiftCount;
                    muted = pulseChannel.timerReload < 8 || (!negateFlag && targetPeriod > 0x7FF);

                    changeAmount *= (negateFlag) ? -1 : 1;

                    if (negateFlag && pulseChannel.pulseId == PULSE1)
                        changeAmount -= 1;

                    targetPeriod = pulseChannel.timerReload + changeAmount;
                }

            private:
                PulseSequencer& pulseChannel;
            }pulseSweeper = SweepUnit(*this);

            void clock()
            {
                if (this->enabled)
                {
                    if (pulseTimer > 0)
                    {
                        pulseTimer--;
                        pulseChannelOutput = dutyCyclePulseTables[dutyCycleTable][tableIndex & 7];
                    }
                    else
                    {
                        pulseTimer = timerReload;
                        tableIndex++;
                    }
                }
            }

            uint8_t output(const EnvelopeGenerator& envelope) const
            {
                if (timerReload < 8 || (!pulseSweeper.negateFlag && pulseSweeper.targetPeriod > 0x7FF))
                    return 0;

                return pulseChannelOutput * envelope.output();
            }
        };

        PulseSequencer pulse1Sequencer;
        PulseSequencer pulse2Sequencer;

        struct TriangleSequencer
        {
            bool enabled = false;
            uint16_t timer = 0x0000;
            uint16_t timerReload = 0x0000;
            uint8_t triangleOutput = 0x00;
            uint8_t stepSequenceIndex = 0;

            void clock(bool countersNonZero)
            {
                if (enabled)
                {
                    if (countersNonZero)
                    {
                        if (timer > 0)
                        {
                            timer--;
                            triangleOutput = triangleStepSequences[stepSequenceIndex & 0x1F];
                        }
                        else
                        {
                            timer = timerReload;
                            stepSequenceIndex++;
                        }
                    }
                }
            }

            uint8_t output() const
            {
                if (timer < 2)
                    return 0;

                return triangleOutput;
            }
        };

        TriangleSequencer triangleSequencer;

        struct NoiseLFSR
        {
            bool enabled = false;
            bool modeFlag = false;
            uint16_t timer = 0x0000;
            uint16_t timerReload = 0x0000;
            uint16_t shiftRegister = 1;

            void clock()
            {
                if (enabled)
                {
                    if (timer > 0)
                    {
                        timer--;
                    }
                    else
                    {
                        timer = timerReload;

                        //if Mode flag is set, XOR bit 0 and bit 6, otherwise, bit 0 with bit 1
                        uint16_t feedback = (modeFlag) ? ((shiftRegister & 0x0001) ^ ((shiftRegister & 0x0040) >> 6)) : ((shiftRegister & 0x0001) ^ ((shiftRegister & 0x0002) >> 1));
                        shiftRegister >>= 1;
                        shiftRegister |= (feedback << 14);
                    }
                }
            }   

            uint8_t output(const EnvelopeGenerator& envelope) const
            {
                // This returns 0 if bit 0 is set
                return (~shiftRegister & 0x0001) * envelope.output(); 
            }
        };

        NoiseLFSR noiseChannelLFSR;

        struct LengthCounter
        {
            uint8_t internalCounter = 0x00;
            bool haltFlag = false;

            void clock(bool enabled)
            {
                if (enabled)
                {
                    if (internalCounter > 0 && !haltFlag)
                    {
                        internalCounter--;
                    }
                }
                else
                {
                    internalCounter = 0;
                }
            }
        };

        LengthCounter pulse1LengthCounter;
        LengthCounter pulse2LengthCounter;
        LengthCounter triangleLengthCounter;
        LengthCounter noiseLengthCounter;

        struct LinearCounter
        {
            bool controlFlag = false;
            uint8_t internalCounter = 0x00;
            uint8_t counterReloadValue = 0x00;
            bool reloadFlag = false;

            void clock(bool enabled)
            {
                if (enabled)
                {
                    if (reloadFlag)
                        internalCounter = counterReloadValue;
                    else if (internalCounter > 0)
                        internalCounter--;

                    if (!controlFlag)
                        reloadFlag = false;
                }
                else
                {
                    internalCounter = 0;
                }
            }
        };

        LinearCounter triangleLinearCounter;

        struct DeltaModulationChannel
        {
            bool enabled = false;
            bool irqEnabledFlag = false;
            bool loopFlag = false;
            uint8_t timerRate = 0x00; // From rateIndex
            uint8_t timer = 0x00;
            uint16_t sampleAddress = 0xC000;
            uint16_t sampleLength = 0x0000;
            std::optional<uint8_t> sampleBuffer = std::nullopt;
            uint16_t currentAddress = 0x0000;
            uint16_t bytesRemaining = 0x0000;
            uint8_t shiftRegister = 0x00;
            uint8_t remainingBits = 8;
            bool silenceFlag = false;
            uint8_t outputLevel = 0x00;

            std::function<uint8_t(uint16_t address)> memoryReaderFunc;

            void clock()
            {
                if (enabled)
                {
                    if (timer > 0)
                    {
                        timer--;
                    }
                    else
                    {
                        if (remainingBits == 0)
                        {
                            remainingBits = 8;
                            
                            if (!sampleBuffer.has_value())
                            {
                                silenceFlag = true;
                            }
                            else
                            {
                                silenceFlag = false;
                                shiftRegister = sampleBuffer.value();
                                sampleBuffer.reset();
                            }

                            if (!sampleBuffer.has_value() && bytesRemaining > 0) // Buffer emptied, time to read next byte
                            {
                                sampleBuffer = memoryReaderFunc(currentAddress);

                                if (currentAddress == 0xFFFF)
                                    currentAddress = 0x8000;
                                else
                                    currentAddress++;

                                bytesRemaining--;

                                if (bytesRemaining == 0 && loopFlag)
                                {
                                    currentAddress = sampleAddress;
                                    bytesRemaining = sampleLength;
                                }
                                else if (bytesRemaining == 0 && irqEnabledFlag)
                                {
                                    APU::apuInstance->dmcInterrupt = true;
                                }
                            }
                        }
                        else
                        {
                            if (!silenceFlag)
                            {
                                if (shiftRegister & 1)
                                {
                                    int8_t result = outputLevel + 2;

                                    if (result <= 127)
                                        outputLevel = result;
                                }
                                else
                                {
                                    int8_t result = outputLevel - 2;

                                    if (result >= 0)
                                        outputLevel = result;
                                }
                            }

                            shiftRegister >>= 1;
                            remainingBits--;
                        }

                        timer = timerRate;
                    }
                }
            }

            uint8_t output() const
            {
                return outputLevel;
            }
        };

        DeltaModulationChannel dmcChannel;

        struct FrameCounterBits
        {
            uint8_t mode = 0;
            uint8_t interruptInhibitFlag = 0;
        };

        FrameCounterBits frameCounter;
        //bool pendingCyclesCounterReset = false;
    };
}