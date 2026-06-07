#!/usr/bin/env python3
from values import ConstraintLocation, Value

# memory constraint
# location: a specific address (e.g. 0x0000) or "auto" for automatic assignment
# length: the size of the constraint in bytes
# name: a unique identifier for the constraint
# initializer: a list of values to initialize the constraint with
# is_const: if True, this constraint is a compile-time constant and is NOT
#           allocated in memory. Its value is inlined wherever it is referenced.
class Constraint:
    def __init__(self, location: ConstraintLocation, length: int, name: str, initializer: list[Value], is_const: bool = False):
        self.location = location
        self.length = length
        self.name = name
        self.initializer = initializer
        self.is_const = is_const