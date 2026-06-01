#!/usr/bin/env python3

# while i am working on an assembler that reads an actual text format,
# i want to be able to work with this much sooner. I will rely on actual
# text input later, and this will use python data structures to define the code
from dataclasses import dataclass
from typing import Literal
import struct

@dataclass
# Represent a definite value as a byte.
class ByteValue:
    value: int

    def to_bytes(self):
        return struct.pack("<B", self.value)
    def length(self):
        return 1
    @classmethod
    def from_bytes(cls, b):
        return cls(struct.unpack("<B", b)[0])

@dataclass
# Represent a definite value as a word.
class WordValue:
    value: int

    def to_bytes(self):
        return struct.pack("<H", self.value)
    def length(self):
        return 2
    @classmethod
    def from_bytes(cls, b):
        return cls(struct.unpack("<H", b)[0])

@dataclass
# Represent a reference to another memory constraint's location by name.
class Reference:
    name: str
    def length(self):
        return 2

ConstraintLocation = WordValue | Literal["auto"]

Value = ByteValue | WordValue | Reference

# memory constraint
# location: a specific address (e.g. 0x0000) or "auto" for automatic assignment
# length: the size of the constraint in bytes (1-65536). In the assembly text, the format of this is instead like "byte", "word", "byte[16]", or "byte[]". corresponding to 1 byte, 2 bytes, 16 bytes, or auto length by the initializer
# name: a unique identifier for the constraint
# initializer: a list of values to initialize the constraint with. This is internally a list of Values, even if the constraint is a word or a single byte. In text, this is optional and has more relaxed syntax. if the constraint does not have a definite length this is still required.
class Constraint:
    def __init__(self, location: ConstraintLocation, length: int, name: str, initializer: list[Value]):
        self.location = location
        self.length = length
        self.name = name
        self.initializer = initializer

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

from ops import OPCODES

def byte_len(list_of_values):
    total = 0
    for value in list_of_values:
        if isinstance(value, Value):
            total += value.length()
        else:
            raise ValueError(f"Invalid value in initializer: {value}")
    return total

if __name__ == "__main__":
    memory = bytearray(65536)

    constraints = [
        Constraint(location=0x0000, length=2, name="PC", initializer=[Reference("start")]),
        Constraint(location=0x0002, length=2, name="SP", initializer=[Reference("stack")]),
        Constraint(location=0x0004, length=2, name="DP", initializer=[Reference("display")]),
        Constraint(location=0x0006, length=2, name="AP", initializer=[Reference("voices")]),
        Constraint(location=0x0008, length=1, name="IN", initializer=[]),
        Constraint(location=0x0009, length=1, name="FL", initializer=[ByteValue(1 << 4)]), # stack grows up
        Constraint(location=0x000A, length=2, name="IM", initializer=[]),
        Constraint(location=0x000C, length=4, name="_rsv", initializer=[]), # might be used in future
        Constraint(
            location="auto", 
            length=byte_len(init_bytes := [
                ByteValue(OPCODES["LOAD16"]), Reference("counter"),
                ByteValue(OPCODES["PUSH16"]), WordValue(1),
                ByteValue(OPCODES["ADD16"]),
                ByteValue(OPCODES["DUP16"]),
                ByteValue(OPCODES["STORE16"]), Reference("counter"),
                ByteValue(OPCODES["PUSH16"]), WordValue(0xFF00),
                ByteValue(OPCODES["CMP16"]),
                ByteValue(OPCODES["JNZ"]), Reference("start"),
            ]), 
            name="start", 
            initializer=init_bytes
        ),            
        Constraint(
            location="auto", 
            length=byte_len(init_bytes := [
                ByteValue(OPCODES["HLT"]),
            ]), 
            name="stop", 
            initializer=init_bytes
        ),
        Constraint(location="auto", length=8, name="stack", initializer=[]),
        Constraint(location="auto", length=2, name="counter", initializer=[]),
        # https://www.eevblog.com/forum/programming/how-do-i-visualize-a-rrrgggbb-color-palette/ initializer=[ByteValue((lambda c, r_i: (lambda c_flipped: (lambda idx: (((idx >> 5) & 7) << 5) | (((idx >> 0) & 7) << 2) | ((idx >> 3) & 3))((r_i ^ 7 if r_i >= 8 else r_i) * 32 + (c_flipped + 16 if r_i >= 8 else c_flipped)))((c ^ 7) if c < 8 else c))((r % 128) // 8, (r // 128) // 8)) for r in range(16384)]
        Constraint(location="auto", length=16384, name="display", initializer=[ByteValue(b) for b in open("colors.bin", "rb").read()]),
        Constraint(location="auto", length=64, name="voices", initializer=[]),
    ]
    assemble(memory, constraints)
    print("Loc\tLen\tName")
    for c in constraints:
        print(f"0x{c.location:04X}\t{c.length}\t{c.name}")
    with open("a.bin", "wb") as f:
        f.write(memory)