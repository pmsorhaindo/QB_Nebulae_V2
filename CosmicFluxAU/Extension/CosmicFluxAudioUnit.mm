#import "CosmicFluxAudioUnit.h"

#import <AVFoundation/AVFoundation.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#include "../dsp/CosmicFluxDSP.h"
#include "../dsp/CosmicFluxParams.h"

using cosmicflux::CosmicFluxDSP;
using cosmicflux::ParamInfo;
using cosmicflux::ParamKind;
using cosmicflux::kNumParams;

static NSString *const kStateKey = @"com.pmsorhaindo.cosmicflux.parameters";

#pragma mark - Host value <-> normalised value

// Hosts see indexed parameters as 0..steps-1 and everything else as 0..1.
static inline float normalizedFromHostValue(int id, AUValue v) {
    const ParamInfo &info = cosmicflux::paramInfo(id);
    if (info.kind == ParamKind::Indexed) return cosmicflux::normalizedFromIndex(id, (int)lrintf(v));
    if (info.kind == ParamKind::Toggle || info.kind == ParamKind::Momentary) return v > 0.5f ? 1.0f : 0.0f;
    return v;
}

static inline AUValue hostValueFromNormalized(int id, float n) {
    const ParamInfo &info = cosmicflux::paramInfo(id);
    if (info.kind == ParamKind::Indexed) return (AUValue)cosmicflux::indexFromNormalized(id, n);
    if (info.kind == ParamKind::Toggle || info.kind == ParamKind::Momentary) return n > 0.5f ? 1.0f : 0.0f;
    return n;
}

#pragma mark - Render-thread state

// Everything the render block touches lives here, reached through a raw pointer
// so the block never retains or messages Objective-C objects.
struct RenderState {
    CosmicFluxDSP *dsp = nullptr;
    AUAudioFrameCount maxFrames = 0;
    std::vector<float> scratchL, scratchR;
    std::atomic<bool> bypass{false};

    // Input bus buffering, after Apple's BufferedAudioBus pattern.
    AVAudioPCMBuffer *pcmBuffer = nil;
    const AudioBufferList *originalABL = nullptr;
    AudioBufferList *mutableABL = nullptr;

    void allocate(AVAudioFormat *format, AUAudioFrameCount frames) {
        maxFrames = frames;
        pcmBuffer = [[AVAudioPCMBuffer alloc] initWithPCMFormat:format frameCapacity:frames];
        originalABL = pcmBuffer.audioBufferList;
        mutableABL = pcmBuffer.mutableAudioBufferList;
        scratchL.assign(frames, 0.0f);
        scratchR.assign(frames, 0.0f);
    }
    void deallocate() {
        pcmBuffer = nil;
        originalABL = nullptr;
        mutableABL = nullptr;
    }
    void prepareInput(AUAudioFrameCount frameCount) {
        UInt32 byteSize = frameCount * sizeof(float);
        for (UInt32 i = 0; i < originalABL->mNumberBuffers; ++i) {
            mutableABL->mBuffers[i].mNumberChannels = originalABL->mBuffers[i].mNumberChannels;
            mutableABL->mBuffers[i].mData = originalABL->mBuffers[i].mData;
            mutableABL->mBuffers[i].mDataByteSize = byteSize;
        }
    }
};

#pragma mark - Factory presets (values from the Nebulae dev scenarios)

struct PresetDef {
    const char *name;
    float values[kNumParams];
};

// Parameter order: Time, Feedback, Dimension, Multiply, CosmosDelay, CosmosDensity,
// CosmosFeedback, Mix, EarlyMod, LateMod, FeedbackFilter, CosmosWarp, CosmosMod,
// CosmosMode, FlangerDepth, Tap, Freeze, Phaser, HalfSpeed, MuteInput, DottedEighth,
// FlangerNegFB, PhaserSync, FlangerMode
static const PresetDef kPresets[] = {
    {"Default", {0.30f, 0.45f, 0.40f, 0.50f, 0.55f, 0.50f, 0.40f, 0.50f, 0.15f, 0.15f, 0.50f, 0.60f, 0.30f, 0.0f, 0.0f,
                 0, 0, 0, 0, 0, 0, 0, 0, 0.5f}},
    {"Subtle Smear", {0.25f, 0.45f, 0.65f, 0.70f, 0.55f, 0.50f, 0.40f, 0.60f, 0.20f, 0.25f, 0.50f, 0.55f, 0.25f, 0.25f, 0.0f,
                      0, 0, 0, 0, 0, 0, 0, 0, 1.0f}},
    {"Rhythmic Multi-Tap", {0.35f, 0.65f, 0.30f, 0.85f, 0.60f, 0.30f, 0.30f, 0.75f, 0.35f, 0.30f, 0.65f, 0.35f, 0.15f, 0.0f, 0.0f,
                            0, 0, 1, 0, 0, 0, 0, 0, 1.0f}},
    {"Cosmic Freeze (press Freeze)", {0.30f, 0.50f, 0.50f, 0.50f, 0.52f, 0.92f, 0.92f, 0.90f, 0.15f, 0.15f, 0.50f, 0.80f, 0.0f, 0.25f, 0.0f,
                                      0, 0, 0, 0, 0, 0, 0, 0, 1.0f}},
};
static const NSInteger kNumPresets = sizeof(kPresets) / sizeof(kPresets[0]);

#pragma mark - Audio unit

@interface CosmicFluxAudioUnit ()
@property (nonatomic, readwrite) AUParameterTree *parameterTree;
@end

@implementation CosmicFluxAudioUnit {
    std::unique_ptr<CosmicFluxDSP> _dsp;
    std::unique_ptr<RenderState> _state;
    AUAudioUnitBus *_inputBus;
    AUAudioUnitBus *_outputBus;
    AUAudioUnitBusArray *_inputBusArray;
    AUAudioUnitBusArray *_outputBusArray;
    AUParameterTree *_parameterTree;
    NSArray<AUAudioUnitPreset *> *_factoryPresets;
    AUAudioUnitPreset *_currentPreset;
}

@synthesize parameterTree = _parameterTree;

- (instancetype)initWithComponentDescription:(AudioComponentDescription)componentDescription
                                     options:(AudioComponentInstantiationOptions)options
                                       error:(NSError **)outError {
    self = [super initWithComponentDescription:componentDescription options:options error:outError];
    if (self == nil) return nil;

    _dsp = std::make_unique<CosmicFluxDSP>();
    _dsp->prepare(48000.0);
    _state = std::make_unique<RenderState>();
    _state->dsp = _dsp.get();

    AVAudioFormat *format = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:48000.0 channels:2];
    _inputBus = [[AUAudioUnitBus alloc] initWithFormat:format error:nil];
    _inputBus.maximumChannelCount = 2;
    _outputBus = [[AUAudioUnitBus alloc] initWithFormat:format error:nil];
    _outputBus.maximumChannelCount = 2;
    _inputBusArray = [[AUAudioUnitBusArray alloc] initWithAudioUnit:self busType:AUAudioUnitBusTypeInput busses:@[ _inputBus ]];
    _outputBusArray = [[AUAudioUnitBusArray alloc] initWithAudioUnit:self busType:AUAudioUnitBusTypeOutput busses:@[ _outputBus ]];

    [self buildParameterTree];
    [self buildFactoryPresets];
    self.maximumFramesToRender = 4096;
    return self;
}

#pragma mark Parameters

- (void)buildParameterTree {
    NSMutableArray<AUParameter *> *params = [NSMutableArray arrayWithCapacity:kNumParams];
    for (int i = 0; i < kNumParams; ++i) {
        const ParamInfo &info = cosmicflux::paramInfo(i);
        AudioUnitParameterOptions flags = kAudioUnitParameterFlag_IsReadable | kAudioUnitParameterFlag_IsWritable;
        AudioUnitParameterUnit unit = kAudioUnitParameterUnit_Generic;
        AUValue minV = 0.0f, maxV = 1.0f;
        NSArray<NSString *> *strings = nil;
        switch (info.kind) {
            case ParamKind::Continuous:
                flags |= kAudioUnitParameterFlag_CanRamp | kAudioUnitParameterFlag_IsHighResolution;
                break;
            case ParamKind::Toggle:
            case ParamKind::Momentary:
                unit = kAudioUnitParameterUnit_Boolean;
                break;
            case ParamKind::Indexed: {
                unit = kAudioUnitParameterUnit_Indexed;
                maxV = (AUValue)(info.numSteps - 1);
                NSMutableArray *s = [NSMutableArray arrayWithCapacity:info.numSteps];
                for (int k = 0; k < info.numSteps; ++k) [s addObject:@(info.valueStrings[k])];
                strings = s;
                break;
            }
        }
        AUParameter *p = [AUParameterTree createParameterWithIdentifier:@(info.identifier)
                                                                   name:@(info.displayName)
                                                                address:(AUParameterAddress)i
                                                                    min:minV
                                                                    max:maxV
                                                                   unit:unit
                                                               unitName:nil
                                                                  flags:flags
                                                           valueStrings:strings
                                                    dependentParameters:nil];
        p.value = hostValueFromNormalized(i, info.defaultValue);
        [params addObject:p];
    }
    _parameterTree = [AUParameterTree createTreeWithChildren:params];

    CosmicFluxDSP *dsp = _dsp.get();
    _parameterTree.implementorValueObserver = ^(AUParameter *param, AUValue value) {
        dsp->setParam((int)param.address, normalizedFromHostValue((int)param.address, value));
    };
    _parameterTree.implementorValueProvider = ^AUValue(AUParameter *param) {
        return hostValueFromNormalized((int)param.address, dsp->getParam((int)param.address));
    };
    _parameterTree.implementorStringFromValueCallback = ^NSString *(AUParameter *param, const AUValue *__nullable valuePtr) {
        AUValue v = valuePtr ? *valuePtr : param.value;
        const ParamInfo &info = cosmicflux::paramInfo((int)param.address);
        switch (info.kind) {
            case ParamKind::Indexed: {
                int idx = (int)lrintf(v);
                if (idx < 0) idx = 0;
                if (idx >= info.numSteps) idx = info.numSteps - 1;
                return @(info.valueStrings[idx]);
            }
            case ParamKind::Toggle:
            case ParamKind::Momentary:
                return v > 0.5f ? @"On" : @"Off";
            default:
                return [NSString stringWithFormat:@"%.0f %%", v * 100.0f];
        }
    };
}

#pragma mark Busses

- (AUAudioUnitBusArray *)inputBusses { return _inputBusArray; }
- (AUAudioUnitBusArray *)outputBusses { return _outputBusArray; }

- (NSArray<NSNumber *> *)channelCapabilities {
    // (inputs, outputs) pairs: stereo in/out, mono in/out, mono in to stereo out.
    return @[ @2, @2, @1, @1, @1, @2, @2, @1 ];
}

- (NSTimeInterval)latency { return 0.0; }
- (NSTimeInterval)tailTime { return 10.0; }

- (void)setShouldBypassEffect:(BOOL)shouldBypassEffect {
    [super setShouldBypassEffect:shouldBypassEffect];
    _state->bypass.store(shouldBypassEffect, std::memory_order_relaxed);
}

#pragma mark Render resources

- (BOOL)allocateRenderResourcesAndReturnError:(NSError **)outError {
    if (![super allocateRenderResourcesAndReturnError:outError]) return NO;

    double sampleRate = _outputBus.format.sampleRate;
    if (sampleRate < 8000.0) sampleRate = _inputBus.format.sampleRate;
    if (sampleRate < 8000.0) sampleRate = 48000.0;

    // All allocation happens here, on the main thread, before rendering starts.
    _dsp->prepare(sampleRate);
    _dsp->reset(true);
    _state->allocate(_inputBus.format, self.maximumFramesToRender);
    _state->bypass.store(self.shouldBypassEffect, std::memory_order_relaxed);
    return YES;
}

- (void)deallocateRenderResources {
    _state->deallocate();
    [super deallocateRenderResources];
}

- (void)reset {
    [super reset];
    _dsp->reset(true);
}

- (AUInternalRenderBlock)internalRenderBlock {
    RenderState *state = _state.get();
    CosmicFluxDSP *dsp = _dsp.get();

    return ^AUAudioUnitStatus(AudioUnitRenderActionFlags *actionFlags,
                              const AudioTimeStamp *timestamp,
                              AVAudioFrameCount frameCount,
                              NSInteger outputBusNumber,
                              AudioBufferList *outputData,
                              const AURenderEvent *realtimeEventListHead,
                              AURenderPullInputBlock pullInputBlock) {
        if (frameCount > state->maxFrames || state->mutableABL == nullptr) return kAudioUnitErr_TooManyFramesToProcess;
        if (pullInputBlock == nil) return kAudioUnitErr_NoConnection;

        // Sample-accurate parameter events from the host are applied at block
        // start; the DSP core smooths them itself.
        for (const AURenderEvent *event = realtimeEventListHead; event != nullptr; event = event->head.next) {
            if (event->head.eventType == AURenderEventParameter || event->head.eventType == AURenderEventParameterRamp) {
                const AUParameterEvent &pe = event->parameter;
                int id = (int)pe.parameterAddress;
                if (id >= 0 && id < kNumParams) dsp->setParam(id, normalizedFromHostValue(id, pe.value));
            }
        }

        AudioUnitRenderActionFlags pullFlags = 0;
        state->prepareInput(frameCount);
        AUAudioUnitStatus err = pullInputBlock(&pullFlags, timestamp, frameCount, 0, state->mutableABL);
        if (err != noErr) return err;

        const AudioBufferList *in = state->mutableABL;
        const float *inL = (const float *)in->mBuffers[0].mData;
        const float *inR = in->mNumberBuffers > 1 ? (const float *)in->mBuffers[1].mData : inL;
        float *outL = state->scratchL.data();
        float *outR = state->scratchR.data();

        if (state->bypass.load(std::memory_order_relaxed)) {
            for (AVAudioFrameCount i = 0; i < frameCount; ++i) { outL[i] = inL[i]; outR[i] = inR[i]; }
        } else {
            dsp->process(inL, inR, outL, outR, (int)frameCount);
        }

        UInt32 byteSize = frameCount * sizeof(float);
        if (outputData->mNumberBuffers >= 2) {
            for (UInt32 c = 0; c < 2; ++c) {
                float *src = c == 0 ? outL : outR;
                outputData->mBuffers[c].mDataByteSize = byteSize;
                if (outputData->mBuffers[c].mData == nullptr) {
                    outputData->mBuffers[c].mData = src;
                } else if (outputData->mBuffers[c].mData != src) {
                    memcpy(outputData->mBuffers[c].mData, src, byteSize);
                }
            }
        } else if (outputData->mNumberBuffers == 1) {
            for (AVAudioFrameCount i = 0; i < frameCount; ++i) outL[i] = 0.5f * (outL[i] + outR[i]);
            outputData->mBuffers[0].mDataByteSize = byteSize;
            if (outputData->mBuffers[0].mData == nullptr) outputData->mBuffers[0].mData = outL;
            else if (outputData->mBuffers[0].mData != outL) memcpy(outputData->mBuffers[0].mData, outL, byteSize);
        }
        return noErr;
    };
}

#pragma mark State save / restore

- (NSDictionary<NSString *, id> *)fullState {
    NSMutableDictionary *state = [[super fullState] mutableCopy];
    if (state == nil) state = [NSMutableDictionary dictionary];
    NSMutableDictionary *values = [NSMutableDictionary dictionaryWithCapacity:kNumParams];
    for (int i = 0; i < kNumParams; ++i) {
        values[@(cosmicflux::paramInfo(i).identifier)] = @(_dsp->getParam(i));
    }
    state[kStateKey] = values;
    return state;
}

- (void)setFullState:(NSDictionary<NSString *, id> *)fullState {
    [super setFullState:fullState];
    NSDictionary *values = fullState[kStateKey];
    if (![values isKindOfClass:[NSDictionary class]]) return;
    for (AUParameter *param in _parameterTree.allParameters) {
        NSNumber *n = values[param.identifier];
        if ([n isKindOfClass:[NSNumber class]]) {
            [param setValue:hostValueFromNormalized((int)param.address, n.floatValue) originator:nil];
        }
    }
}

#pragma mark Presets

- (void)buildFactoryPresets {
    NSMutableArray *presets = [NSMutableArray arrayWithCapacity:kNumPresets];
    for (NSInteger i = 0; i < kNumPresets; ++i) {
        AUAudioUnitPreset *p = [[AUAudioUnitPreset alloc] init];
        p.number = i;
        p.name = @(kPresets[i].name);
        [presets addObject:p];
    }
    _factoryPresets = presets;
    _currentPreset = presets.firstObject;
}

- (NSArray<AUAudioUnitPreset *> *)factoryPresets { return _factoryPresets; }
- (BOOL)supportsUserPresets { return YES; }
- (AUAudioUnitPreset *)currentPreset { return _currentPreset; }

- (void)setCurrentPreset:(AUAudioUnitPreset *)currentPreset {
    if (currentPreset == nil) return;
    if (currentPreset.number >= 0 && currentPreset.number < kNumPresets) {
        const PresetDef &def = kPresets[currentPreset.number];
        for (AUParameter *param in _parameterTree.allParameters) {
            int id = (int)param.address;
            [param setValue:hostValueFromNormalized(id, def.values[id]) originator:nil];
        }
        _currentPreset = _factoryPresets[currentPreset.number];
    } else {
        // User preset: the host restores it through setFullState.
        NSError *err = nil;
        NSDictionary *state = [self presetStateFor:currentPreset error:&err];
        if (state != nil) [self setFullState:state];
        _currentPreset = currentPreset;
    }
}

@end
