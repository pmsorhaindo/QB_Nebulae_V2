#!/usr/bin/env python3
"""
Basic syntax validation for CosmicFlux instrument

Checks for common Csound syntax issues without requiring Csound to be installed.
"""

import re
from pathlib import Path

INSTR_FILE = Path(__file__).parent.parent / "cosmicflux.instr"

def validate_instrument():
    """Perform basic syntax validation"""
    
    print("Validating CosmicFlux instrument syntax...")
    print(f"File: {INSTR_FILE}")
    
    with open(INSTR_FILE, 'r') as f:
        content = f.read()
    
    issues = []
    
    # Check for nebconfig section
    if "nebconfigbegin" not in content or "nebconfigend" not in content:
        issues.append("Missing nebconfig section")
    
    # Check for instr 1 (required)
    if not re.search(r'\binstr\s+1\b', content):
        issues.append("Missing 'instr 1' definition")
    
    # Check for endin (must match instr count)
    instr_count = len(re.findall(r'\binstr\s+\d+', content))
    endin_count = content.count('endin')
    if instr_count != endin_count:
        issues.append(f"Mismatched instr/endin count: {instr_count} instr, {endin_count} endin")
    
    # Check for unmatched delayr/delayw
    delayr_count = content.count('delayr')
    delayw_count = content.count('delayw')
    if delayr_count != delayw_count:
        issues.append(f"Mismatched delayr/delayw: {delayr_count} delayr, {delayw_count} delayw")
    
    # Check for common opcodes that should be present
    required_opcodes = ['inch', 'outs', 'portk', 'deltap3']
    for opcode in required_opcodes:
        if opcode not in content:
            issues.append(f"Expected opcode '{opcode}' not found")
    
    # Check for unmatched if/endif
    if_count = len(re.findall(r'\bif\b', content))
    elseif_count = len(re.findall(r'\belseif\b', content))
    endif_count = len(re.findall(r'\bendif\b', content))
    then_count = len(re.findall(r'\bthen\b', content))
    
    # Each 'if' and 'elseif' should have a 'then', and each control structure should have 'endif'
    if if_count != endif_count:
        issues.append(f"Mismatched if/endif: {if_count} if, {endif_count} endif")
    if if_count + elseif_count != then_count:
        issues.append(f"Mismatched if+elseif/then: {if_count + elseif_count} if+elseif, {then_count} then")
    
    # Check for init declarations (should use 'init' keyword)
    init_count = content.count(' init ')
    if init_count < 5:
        issues.append(f"Very few init declarations ({init_count}) - might be missing initialization")
    
    # Check for denorm (should be present for reverb/delay tails)
    if 'denorm' not in content:
        issues.append("No 'denorm' opcode found - recommended for preventing denormal CPU issues")
    
    # Check for dcblock (should be in feedback loops)
    if 'dcblock' not in content:
        issues.append("No 'dcblock' opcode found - recommended for preventing DC buildup")
    
    # Check for clip or limiter
    if 'clip' not in content and 'tanh' not in content:
        issues.append("No 'clip' or 'tanh' found - output may not be limited")
    
    # Check line count (too short might indicate incomplete file)
    lines = content.split('\n')
    if len(lines) < 100:
        issues.append(f"File is very short ({len(lines)} lines) - might be incomplete")
    
    # Report
    if issues:
        print("\n⚠ Potential issues found:")
        for issue in issues:
            print(f"  - {issue}")
        print("\nNote: These are basic checks. The instrument may still be valid.")
        print("Run through Csound for definitive validation.")
        return False
    else:
        print("\n✓ Basic syntax validation passed!")
        print(f"  - {instr_count} instrument(s) defined")
        print(f"  - {delayr_count} delay line(s)")
        print(f"  - {len(lines)} lines of code")
        print("\nReady for Csound compilation test.")
        return True

if __name__ == "__main__":
    import sys
    success = validate_instrument()
    sys.exit(0 if success else 1)
