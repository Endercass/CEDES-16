#!/usr/bin/env python3
import sys
import re
import struct

from values import ByteValue, WordValue, Reference, ConstExpr, byte_len, Value
from constraints import Constraint
from assemble import assemble
from ops import OPCODES

TOKEN_SPECIFICATION = [
    ("STRING",      r'"[^"]*"'),
    ("CONSTEXPR",   r'#\((?:[^()]*|\([^()]*\))*\)'),
    ("HEXNUMBER",   r'0x[0-9A-Fa-f]+'),
    ("NUMBER",      r"\d+"),
    ("IDENT",       r"[A-Za-z_][A-Za-z0-9_]*"),
    ("LBRACE",      r"\{"),
    ("RBRACE",      r"\}"),
    ("LBRACK",      r"\["),
    ("RBRACK",      r"\]"),
    ("EQUAL",       r"="),
    ("LANGLE",      r"<"),
    ("COMMA",       r","),
    ("SEMI",        r";"),
    ("WS",          r"[ \t]+"),
    ("NEWLINE",     r"\n"),
    ("OTHER",       r".")
]

master_pat = re.compile("|".join(f"(?P<{name}>{pat})" for name, pat in TOKEN_SPECIFICATION))

def tokenize(text):
    line_num = 1
    line_start = 0
    for mo in master_pat.finditer(text):
        kind = mo.lastgroup
        value = mo.group()
        col = mo.start() - line_start + 1
        if kind == "NEWLINE":
            line_num += 1
            line_start = mo.end()
            continue
        if kind == "WS":
            continue
        yield (line_num, col, kind, value)

def preprocess_strip_comments(text):
    return re.sub(r"//.*", "", text)


def build_const_env(constraints: list[Constraint]) -> dict:
    env = {}
    for c in constraints:
        if not c.is_const:
            if c.location != "auto":
                env[c.name] = c.location
        else:
            if len(c.initializer) == 1:
                v = c.initializer[0]
                if isinstance(v, (ByteValue, WordValue)):
                    env[c.name] = v.value
                elif isinstance(v, ConstExpr):
                    # Will be resolved in a second pass
                    pass
    return env


def eval_constexpr(expr_str: str, constraints: list[Constraint], env: dict) -> int:
    safe_env = dict(env)  # copy

    def replace_sizeof(m):
        name = m.group(1).strip()
        for c in constraints:
            if c.name == name:
                return str(c.length)
        raise ValueError(f"sizeof: unknown constraint '{name}'")

    expr = re.sub(r'\bsizeof\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)', replace_sizeof, expr_str)

    try:
        result = eval(expr, {"__builtins__": {}}, safe_env)
    except Exception as e:
        raise ValueError(f"Failed to evaluate const expression '#({expr_str})': {e}")

    if not isinstance(result, int):
        raise ValueError(f"Const expression '#({expr_str})' did not evaluate to an integer (got {result!r})")

    return result & 0xFFFF  # clamp to 16-bit


def resolve_constexprs(constraints: list[Constraint]):
    env = build_const_env(constraints)

    for c in constraints:
        if c.is_const and len(c.initializer) == 1 and isinstance(c.initializer[0], ConstExpr):
            val = eval_constexpr(c.initializer[0].expr_str, constraints, env)
            if val <= 0xFF and not c.initializer[0].force_word:
                c.initializer[0] = ByteValue(val)
            else:
                c.initializer[0] = WordValue(val)
            env[c.name] = val 

    env = build_const_env(constraints)

    for c in constraints:
        for i, v in enumerate(c.initializer):
            if isinstance(v, ConstExpr):
                val = eval_constexpr(v.expr_str, constraints, env)
                if val <= 0xFF and not v.force_word:
                    c.initializer[i] = ByteValue(val)
                else:
                    c.initializer[i] = WordValue(val)


class Parser:
    def __init__(self, tokens):
        self.tokens = tokens
        self.pos = 0

    def peek(self):
        if self.pos < len(self.tokens):
            return self.tokens[self.pos]
        return None

    def match(self, kind):
        tok = self.peek()
        if tok and tok[2] == kind:
            self.pos += 1
            return tok
        return None
    
    def expect(self, kind):
        tok = self.match(kind)
        if not tok:
            raise SyntaxError(f"Expected {kind}, got {self.peek()}")
        return tok
    
    def parse_location(self):
        self.expect("LBRACK")
        if tok := self.match("HEXNUMBER"):
            loc = int(tok[3], 16)
        elif tok := self.match("NUMBER"):
            loc = int(tok[3], 10)
        elif tok := self.match("IDENT"):
            if tok[3] == "auto":
                loc = "auto"
            elif tok[3] == "const":
                loc = "const"
            else:
                raise SyntaxError(f"Invalid location: {tok[3]}")
        else:
            raise SyntaxError("Expected location in brackets")
        self.expect("RBRACK")
        return loc

    def parse_type(self):
        tok = self.expect("IDENT")
        base_type = tok[3]
        if base_type not in ("byte", "word"):
            raise SyntaxError(f"Unknown type {base_type}")
        
        is_array = False
        array_size = None
        if self.match("LBRACK"):
            is_array = True
            if num := self.match("NUMBER"):
                array_size = int(num[3], 10)
            elif num := self.match("HEXNUMBER"):
                array_size = int(num[3], 16)
            elif num := self.match("RBRACK"):
                self.pos -= 1
            self.expect("RBRACK")
        return base_type, is_array, array_size

    def parse_value_item(self):
        tok = self.peek()
        if not tok:
            raise SyntaxError("Unexpected EOF in value list")

        if tok[2] == "CONSTEXPR":
            self.pos += 1
            expr_str = tok[3][2:-1]  # strip #( and )
            # Check if prefixed with word modifier
            return ConstExpr(expr_str.strip())

        explicit_word = False
        explicit_byte = False
        if tok[2] == "IDENT" and tok[3] in ("word", "byte"):
            if tok[3] == "word":
                explicit_word = True
            if tok[3] == "byte":
                explicit_byte = True
            self.pos += 1
            tok = self.peek()
            if not tok:
                raise SyntaxError("Unexpected EOF after modifier")

            if tok[2] == "CONSTEXPR":
                self.pos += 1
                expr_str = tok[3][2:-1]
                return ConstExpr(expr_str.strip(), force_word=explicit_word)

        if tok[2] == "IDENT":
            self.pos += 1
            if tok[3] in OPCODES:
                val = ByteValue(OPCODES[tok[3]])
                if explicit_word:
                    val = WordValue(val.value)
                return val
            else:
                return Reference(tok[3])
        elif tok[2] in ("NUMBER", "HEXNUMBER"):
            self.pos += 1
            num = int(tok[3], 16) if tok[2] == "HEXNUMBER" else int(tok[3], 10)
            if explicit_word or num > 255:
                return WordValue(num)
            else:
                return ByteValue(num)
        else:
            raise SyntaxError(f"Unexpected token in value list: {tok}")

    def parse_constraint(self):
        if not self.peek():
            return None
        loc = self.parse_location()
        is_const = (loc == "const")

        base_type, is_array, array_size = self.parse_type()
        name = self.expect("IDENT")[3]
        
        initializer = []
        has_init = False
        
        if self.match("LANGLE"):
            has_init = True
            path_tok = self.expect("STRING")
            path = path_tok[3][1:-1]
            with open(path, "rb") as f:
                data = f.read()
            if base_type == "byte":
                initializer = [ByteValue(b) for b in data]
            else:
                for i in range(0, len(data), 2):
                    if i+1 < len(data):
                        v = struct.unpack("<H", data[i:i+2])[0]
                        initializer.append(WordValue(v))
                    else:
                        initializer.append(WordValue(data[i]))
        elif self.match("EQUAL"):
            has_init = True
            if self.match("LBRACE"):
                while not self.match("RBRACE"):
                    val = self.parse_value_item()
                    initializer.append(val)
                    self.match("SEMI")
            else:
                val = self.parse_value_item()
                initializer.append(val)

        if is_const:
            if not has_init:
                raise SyntaxError(f"[const] constraint '{name}' must have an initializer")
            if is_array:
                raise SyntaxError(f"[const] constraint '{name}' cannot be an array")
            return Constraint(location="const", length=0, name=name, initializer=initializer, is_const=True)

        # Calculate length for normal constraints
        if not is_array and not has_init:
            length = 2 if base_type == "word" else 1
        elif not is_array and has_init:
            length = byte_len(initializer)
        elif is_array and not has_init:
            if array_size is None:
                raise SyntaxError(f"Array {name} must have a size or an initializer")
            length = array_size * (2 if base_type == "word" else 1)
        elif is_array and has_init:
            calc_len = byte_len(initializer)
            if array_size is not None:
                expected = array_size * (2 if base_type == "word" else 1)
                length = expected
            else:
                length = calc_len
                
        return Constraint(location=loc, length=length, name=name, initializer=initializer, is_const=False)

    def parse(self):
        constraints = []
        while self.peek():
            c = self.parse_constraint()
            if c:
                constraints.append(c)
        return constraints

def read_and_parse(path):
    with open(path, "r", encoding="utf-8") as f:
        text = f.read()
    text = preprocess_strip_comments(text)
    tokens = list(tokenize(text))
    parser = Parser(tokens)
    return parser.parse()

def main(path):
    constraints = read_and_parse(path)

    memory = bytearray(65536)
    assemble(memory, constraints)

    resolve_constexprs(constraints)

    from assemble import write_initializers
    write_initializers(memory, constraints)

    out_path = path.replace(".cxt", ".bin") if path.endswith(".cxt") else path + ".bin"
    with open(out_path, "wb") as f:
        f.write(memory)
    print(f"Assembled {path} -> {out_path}")
    print("Loc\tLen\tName\t\tConst?")
    for c in constraints:
        loc_str = "const" if c.is_const else f"0x{c.location:04X}"
        print(f"{loc_str}\t{c.length}\t{c.name}\t\t{'yes' if c.is_const else ''}")

if __name__ == "__main__":
    path = sys.argv[1] if len(sys.argv) > 1 else "test.cxt"
    main(path)