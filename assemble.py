#!/usr/bin/env python3
from constraints import Constraint
from values import ByteValue, WordValue, Reference


def assemble(memory: bytearray, constraints: list[Constraint]):
    # 1. Sort constraints safely (putting "auto" last)
    constraints.sort(key=lambda c: (1 if c.location == "auto" else 0, c.location if c.location != "auto" else 0))
    
    last_address = 0
    # 2. Resolve auto locations and check bounds
    for constraint in constraints:
        if constraint.location == "auto":
            constraint.location = last_address
            
        if constraint.location < last_address:
            raise ValueError(f"Constraint {constraint.name} overlaps with previous constraint")
        if constraint.location + constraint.length > len(memory):
            raise ValueError(f"Constraint {constraint.name} exceeds memory size")
            
        last_address = constraint.location + constraint.length
    
    # 3. Write initializers to memory using tracking byte offsets
    for constraint in constraints:
        current_offset = constraint.location
        for value in constraint.initializer:
            if isinstance(value, Reference):
                # Safely find the target constraint
                try:
                    ref_constraint = next(c for c in constraints if c.name == value.name)
                    resolved_value = WordValue(ref_constraint.location)
                except StopIteration:
                    raise ValueError(f"Undefined reference to symbol: '{value.name}'")
            else:
                resolved_value = value

            # Extract ALL bytes from the object data
            serialized_bytes = resolved_value.to_bytes()
            
            # Write the block into memory and advance the cursor correctly
            for b in serialized_bytes:
                memory[current_offset] = b
                current_offset += 1
