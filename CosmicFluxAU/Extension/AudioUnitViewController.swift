//
//  AudioUnitViewController.swift
//  CosmicFluxAUAudioUnit
//
//  AUv3 Audio Unit Extension
//

import CoreAudioKit

public class AudioUnitViewController: AUViewController, AUAudioUnitFactory {
    
    public override func viewDidLoad() {
        super.viewDidLoad()
        
        view.backgroundColor = .black
        
        let label = UILabel()
        label.text = "CosmicFluxAU"
        label.textColor = .white
        label.textAlignment = .center
        label.font = .systemFont(ofSize: 20, weight: .regular)
        label.translatesAutoresizingMaskIntoConstraints = false
        
        view.addSubview(label)
        
        NSLayoutConstraint.activate([
            label.centerXAnchor.constraint(equalTo: view.centerXAnchor),
            label.centerYAnchor.constraint(equalTo: view.centerYAnchor)
        ])
    }
    
    // MARK: - AUAudioUnitFactory
    
    public func createAudioUnit(with componentDescription: AudioComponentDescription) throws -> AUAudioUnit {
        return try CosmicFluxAUAudioUnit(componentDescription: componentDescription, options: [])
    }
}

@objc public func CosmicFluxAUAudioUnitFactory(componentDescription: AudioComponentDescription) -> AUAudioUnit? {
    return try? CosmicFluxAUAudioUnit(componentDescription: componentDescription, options: [])
}
