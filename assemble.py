#!/usr/bin/env python3
from constraints import Constraint
from values import ByteValue, WordValue, Reference, ConstExpr


def write_initializers(memory: bytearray, constraints: list[Constraint], skip_constexpr: bool = False):
    for constraint in constraints:
        if constraint.is_const:
            continue
        current_offset = constraint.location
        for value in constraint.initializer:
            if isinstance(value, ConstExpr):
                if skip_constexpr:
                    memory[current_offset] = 0
                    memory[current_offset + 1] = 0
                    current_offset += 2
                    continue
                else:
                    raise ValueError(
                        f"Unresolved ConstExpr in '{constraint.name}': #({value.expr_str})"
                    )
            elif isinstance(value, Reference):
                try:
                    ref_constraint = next(c for c in constraints if c.name == value.name)
                    if ref_constraint.is_const:
                        if len(ref_constraint.initializer) == 1:
                            resolved_value = ref_constraint.initializer[0]
                        else:
                            raise ValueError(f"[const] '{value.name}' has no single value to inline")
                    else:
                        resolved_value = WordValue(ref_constraint.location)
                except StopIteration:
                    raise ValueError(f"Undefined reference to symbol: '{value.name}'")
            else:
                resolved_value = value

            serialized_bytes = resolved_value.to_bytes()
            for b in serialized_bytes:
                memory[current_offset] = b
                current_offset += 1


def assemble(memory: bytearray, constraints: list[Constraint]):
    non_const = [c for c in constraints if not c.is_const]

    non_const.sort(key=lambda c: (1 if c.location == "auto" else 0, c.location if c.location != "auto" else 0))

    last_address = 0
    for constraint in non_const:
        if constraint.location == "auto":
            constraint.location = last_address

        if constraint.location < last_address:
            raise ValueError(f"Constraint {constraint.name} overlaps with previous constraint")
        if constraint.location + constraint.length > len(memory):
            raise ValueError(f"Constraint {constraint.name} exceeds memory size")

        last_address = constraint.location + constraint.length

    write_initializers(memory, constraints, skip_constexpr=True)