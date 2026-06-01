#!/usr/bin/env python3
from values import ConstraintLocation, Value

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
