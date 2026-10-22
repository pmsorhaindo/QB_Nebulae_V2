//
//  ViewController.swift
//  CosmicFluxAU
//
//  iOS AUv3 Host Application
//

import UIKit
import AVFoundation

class ViewController: UIViewController {
    
    override func viewDidLoad() {
        super.viewDidLoad()
        
        view.backgroundColor = .systemBackground
        
        let label = UILabel()
        label.text = "CosmicFluxAU Host"
        label.textAlignment = .center
        label.font = .systemFont(ofSize: 24, weight: .medium)
        label.translatesAutoresizingMaskIntoConstraints = false
        
        view.addSubview(label)
        
        NSLayoutConstraint.activate([
            label.centerXAnchor.constraint(equalTo: view.centerXAnchor),
            label.centerYAnchor.constraint(equalTo: view.centerYAnchor)
        ])
    }
}
