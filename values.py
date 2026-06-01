#!/usr/bin/env python3
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


def byte_len(list_of_values):
    total = 0
    for value in list_of_values:
        if isinstance(value, Value):
            total += value.length()
        else:
            raise ValueError(f"Invalid value in initializer: {value}")
    return total
