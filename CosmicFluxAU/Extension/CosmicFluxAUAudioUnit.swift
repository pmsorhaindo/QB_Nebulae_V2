//
//  CosmicFluxAUAudioUnit.swift
//  CosmicFluxAUAudioUnit
//
//  AUv3 Audio Unit Implementation
//

import AVFoundation
import CoreAudioKit

public class CosmicFluxAUAudioUnit: AUAudioUnit {
    
    private var inputBus: AUAudioUnitBus
    private var outputBus: AUAudioUnitBus
    private var _inputBusArray: AUAudioUnitBusArray!
    private var _outputBusArray: AUAudioUnitBusArray!
    
    public override init(componentDescription: AudioComponentDescription, options: AudioComponentInstantiationOptions = []) throws {
        
        // Create default audio format
        let format = AVAudioFormat(standardFormatWithSampleRate: 44100, channels: 2)!
        
        // Initialize buses
        inputBus = try AUAudioUnitBus(format: format)
        inputBus.maximumChannelCount = 2
        
        outputBus = try AUAudioUnitBus(format: format)
        outputBus.maximumChannelCount = 2
        
        try super.init(componentDescription: componentDescription, options: options)
        
        // Create bus arrays
        _inputBusArray = AUAudioUnitBusArray(audioUnit: self, busType: .input, busses: [inputBus])
        _outputBusArray = AUAudioUnitBusArray(audioUnit: self, busType: .output, busses: [outputBus])
    }
    
    public override var inputBusses: AUAudioUnitBusArray {
        return _inputBusArray
    }
    
    public override var outputBusses: AUAudioUnitBusArray {
        return _outputBusArray
    }
    
    public override func allocateRenderResources() throws {
        try super.allocateRenderResources()
    }
    
    public override func deallocateRenderResources() {
        super.deallocateRenderResources()
    }
    
    public override var internalRenderBlock: AUInternalRenderBlock {
        return { (actionFlags, timestamp, frameCount, outputBusNumber, outputData, realtimeEventListHead, pullInputBlock) in
            
            // Pass through audio for now
            if let pullInputBlock = pullInputBlock {
                var pullFlags = AudioUnitRenderActionFlags(rawValue: 0)
                let status = pullInputBlock(&pullFlags, timestamp, frameCount, 0, outputData)
                return status
            }
            
            return noErr
        }
    }
}
