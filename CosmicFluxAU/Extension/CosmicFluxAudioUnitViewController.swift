// Principal class of the AUv3 extension: creates the audio unit for the host
// (AUAudioUnitFactory) and provides a simple UIKit parameter view. AUM also
// offers its own generic parameter view, so this UI is deliberately plain.
import AudioToolbox
import CoreAudioKit
import UIKit

@objc(CosmicFluxAudioUnitViewController)
public class CosmicFluxAudioUnitViewController: AUViewController, AUAudioUnitFactory {

    private var audioUnit: CosmicFluxAudioUnit?
    private var observerToken: AUParameterObserverToken?
    private var controls: [AUParameterAddress: UIControl] = [:]
    private var valueLabels: [AUParameterAddress: UILabel] = [:]
    private let stack = UIStackView()

    private struct Section {
        let title: String
        let identifiers: [String]
    }

    private let sections: [Section] = [
        Section(title: "Flux Chain", identifiers: ["time", "feedback", "dimension", "multiply", "early_mod", "late_mod", "feedback_filter", "half_speed", "dotted_eighth", "tap"]),
        Section(title: "Flanger", identifiers: ["flanger_depth", "flanger_mode", "flanger_neg_fb"]),
        Section(title: "Phaser", identifiers: ["phaser", "phaser_sync"]),
        Section(title: "Cosmos", identifiers: ["cosmos_delay", "cosmos_density", "cosmos_feedback", "cosmos_warp", "cosmos_mod", "cosmos_mode", "freeze"]),
        Section(title: "Output", identifiers: ["mix", "mute_input"]),
    ]

    // MARK: AUAudioUnitFactory

    public func createAudioUnit(with componentDescription: AudioComponentDescription) throws -> AUAudioUnit {
        let unit = try CosmicFluxAudioUnit(componentDescription: componentDescription, options: [])
        audioUnit = unit
        DispatchQueue.main.async { [weak self] in
            guard let self = self, self.isViewLoaded else { return }
            self.connectParameters()
        }
        return unit
    }

    // MARK: View lifecycle

    public override func viewDidLoad() {
        super.viewDidLoad()
        preferredContentSize = CGSize(width: 480, height: 720)
        view.backgroundColor = UIColor.systemBackground
        buildUI()
        connectParameters()
    }

    deinit {
        if let token = observerToken {
            audioUnit?.parameterTree?.removeParameterObserver(token)
        }
    }

    // MARK: UI construction

    private func buildUI() {
        let scroll = UIScrollView()
        scroll.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(scroll)

        stack.axis = .vertical
        stack.spacing = 8
        stack.translatesAutoresizingMaskIntoConstraints = false
        scroll.addSubview(stack)

        NSLayoutConstraint.activate([
            scroll.topAnchor.constraint(equalTo: view.topAnchor),
            scroll.bottomAnchor.constraint(equalTo: view.bottomAnchor),
            scroll.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            scroll.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            stack.topAnchor.constraint(equalTo: scroll.contentLayoutGuide.topAnchor, constant: 12),
            stack.bottomAnchor.constraint(equalTo: scroll.contentLayoutGuide.bottomAnchor, constant: -12),
            stack.leadingAnchor.constraint(equalTo: scroll.contentLayoutGuide.leadingAnchor, constant: 12),
            stack.trailingAnchor.constraint(equalTo: scroll.contentLayoutGuide.trailingAnchor, constant: -12),
            stack.widthAnchor.constraint(equalTo: scroll.frameLayoutGuide.widthAnchor, constant: -24),
        ])

        let title = UILabel()
        title.text = "CosmicFlux"
        title.font = UIFont.boldSystemFont(ofSize: 22)
        stack.addArrangedSubview(title)
    }

    private func connectParameters() {
        guard let unit = audioUnit, let tree = unit.parameterTree else { return }
        guard controls.isEmpty else { return }

        for section in sections {
            let header = UILabel()
            header.text = section.title.uppercased()
            header.font = UIFont.systemFont(ofSize: 12, weight: .semibold)
            header.textColor = UIColor.secondaryLabel
            stack.addArrangedSubview(header)
            for identifier in section.identifiers {
                guard let param = tree.value(forKey: identifier) as? AUParameter else { continue }
                stack.addArrangedSubview(makeRow(for: param))
            }
        }

        observerToken = tree.token(byAddingParameterObserver: { [weak self] address, value in
            DispatchQueue.main.async { self?.updateControl(address: address, value: value) }
        })
        for param in tree.allParameters {
            updateControl(address: param.address, value: param.value)
        }
    }

    private func makeRow(for param: AUParameter) -> UIView {
        let row = UIStackView()
        row.axis = .horizontal
        row.spacing = 8
        row.alignment = .center

        let name = UILabel()
        name.text = param.displayName
        name.font = UIFont.systemFont(ofSize: 14)
        name.widthAnchor.constraint(equalToConstant: 130).isActive = true
        row.addArrangedSubview(name)

        let valueLabel = UILabel()
        valueLabel.font = UIFont.monospacedDigitSystemFont(ofSize: 13, weight: .regular)
        valueLabel.textColor = UIColor.secondaryLabel
        valueLabel.textAlignment = .right
        valueLabel.widthAnchor.constraint(equalToConstant: 96).isActive = true

        let control: UIControl
        switch param.unit {
        case .boolean:
            if param.identifier == "tap" {
                let button = UIButton(type: .system)
                button.setTitle("Tap", for: .normal)
                button.addTarget(self, action: #selector(tapPressed(_:)), for: .touchUpInside)
                control = button
            } else {
                let sw = UISwitch()
                sw.addTarget(self, action: #selector(switchChanged(_:)), for: .valueChanged)
                control = sw
            }
        case .indexed:
            let seg = UISegmentedControl(items: param.valueStrings ?? [])
            seg.addTarget(self, action: #selector(segmentChanged(_:)), for: .valueChanged)
            control = seg
        default:
            let slider = UISlider()
            slider.minimumValue = param.minValue
            slider.maximumValue = param.maxValue
            slider.addTarget(self, action: #selector(sliderChanged(_:)), for: .valueChanged)
            control = slider
        }
        control.tag = Int(param.address)
        controls[param.address] = control
        valueLabels[param.address] = valueLabel
        row.addArrangedSubview(control)
        row.addArrangedSubview(valueLabel)
        return row
    }

    private func updateControl(address: AUParameterAddress, value: AUValue) {
        guard let param = audioUnit?.parameterTree?.parameter(withAddress: address) else { return }
        valueLabels[address]?.text = param.string(fromValue: nil)
        switch controls[address] {
        case let slider as UISlider:
            if !slider.isTracking { slider.value = value }
        case let sw as UISwitch:
            sw.setOn(value > 0.5, animated: false)
        case let seg as UISegmentedControl:
            seg.selectedSegmentIndex = Int(value.rounded())
        default:
            break
        }
    }

    private func parameter(for control: UIControl) -> AUParameter? {
        return audioUnit?.parameterTree?.parameter(withAddress: AUParameterAddress(control.tag))
    }

    // MARK: Actions

    @objc private func sliderChanged(_ sender: UISlider) {
        parameter(for: sender)?.setValue(sender.value, originator: observerToken)
        if let address = parameter(for: sender)?.address { updateControl(address: address, value: sender.value) }
    }

    @objc private func switchChanged(_ sender: UISwitch) {
        parameter(for: sender)?.setValue(sender.isOn ? 1 : 0, originator: observerToken)
    }

    @objc private func segmentChanged(_ sender: UISegmentedControl) {
        parameter(for: sender)?.setValue(AUValue(sender.selectedSegmentIndex), originator: observerToken)
    }

    @objc private func tapPressed(_ sender: UIButton) {
        guard let param = parameter(for: sender) else { return }
        param.setValue(1, originator: observerToken)
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.05) {
            param.setValue(0, originator: self.observerToken)
        }
    }
}
