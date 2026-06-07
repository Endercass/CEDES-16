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

@dataclass
# Represent a compile-time expression like #(PALETTE_BASE + 3 * 8).
# expr_str: the raw string inside #(...) for deferred evaluation.
# force_word: if True, always resolves to a WordValue; otherwise byte if fits.
class ConstExpr:
    expr_str: str
    force_word: bool = False
    def length(self):
        # Conservative 2 bytes until resolved; assembler corrects after eval.
        return 2

ConstraintLocation = WordValue | Literal["auto"]

Value = ByteValue | WordValue | Reference | ConstExpr


def byte_len(initializer):
    return sum(v.length() for v in initializer)