"""Synthetic fuse maps and a small Boolean evaluator for equation tests."""

import re


def put_term(bits, address, width, literals, columns):
    """Disconnect a row, then connect just the requested input literals."""
    bits[address:address + width] = [1] * width
    for literal in literals:
        inverted = literal.startswith("!")
        name = literal.lstrip("!")
        bits[address + columns[name] + inverted] = 0


def probe_map(device, products):
    bits = [0] * device.fuses
    columns = {"X": device.x_fuse, "Y": device.y_fuse}
    stride = device.width
    if device.kind == "V8":
        base = 64 * device.width
        bits[base] = 1                 # non-inverting XOR
        bits[base + 144] = 1           # small combinational mode
    elif device.kind == "V10":
        bits[5808:5810] = [1, 1]       # non-inverting, combinational
        put_term(bits, 44, 44, (), {}) # enable the selected output
    elif device.kind == "6001":
        stride = 114
        for row in range(64):
            bits[row * stride + 78:row * stride + 114] = [1] * 36
        bits[8178:8182] = [0, 1, 0, 1]
        put_term(bits, 7998, 78, (), {}) # enable physical output 14 (DIP)
    elif device.kind == "MAPL":
        stride = 112
        for row in range(128):
            bits[row * stride + 58:row * stride + 112] = [1] * 54
        node = 11 if device.name == "MAPL128" else 3
        bits[14568 + node] = bits[14595 + node] = 1 # positive D/CE planes
    for row, literals in enumerate(products):
        address = device.address + row * stride
        put_term(bits, address, device.width, literals, columns)
        if device.kind == "6001":
            bits[address + 78] = 0    # connect data OR column zero
        elif device.kind == "MAPL":
            bits[address + 58 + 2 * node] = 0
    return bits


def jedec(device, bits, labels=None):
    if labels is None:
        labels = {device.x: "X", device.y: "Y", device.output: "OUT"}
        if device.enable_pin:
            labels[device.enable_pin] = "EN"
    assert len(bits) == device.fuses
    fields = [f"\x02{device.name}*", f"QF{device.fuses}*QP{device.pins}*F0*"]
    fields.extend(f"L{offset} " + "".join(map(str, bits[offset:offset + 64])) + "*"
                  for offset in range(0, len(bits), 64))
    fields.append("NOTE PINS " + " ".join(f"{name}:{pin}" for pin, name in labels.items()) + "*")
    return ("\n".join(fields) + "\n\x030000").encode("ascii")


def assignments(text):
    """Read assignments and continuations without depending on whitespace."""
    result = {}
    current = None
    for line in text.split("\nequations\n", 1)[1].splitlines():
        line = line.strip()
        if not line:
            continue
        match = re.fullmatch(r"([/!]?\w+(?:\.\w+)?)\s*(:=|=)\s*(.*)", line)
        if match:
            current, operator, expression = match.groups()
            if current in result:
                raise AssertionError(f"duplicate assignment: {current}")
            result[current] = (operator, expression)
        elif current and line.startswith(("+", "#", "*", "&", ":+:", "$")):
            operator, expression = result[current]
            result[current] = (operator, expression + " " + line)
        else:
            raise AssertionError(f"unrecognized equation line: {line}")
    return result


def evaluate(expression, values, definitions=None):
    """Evaluate NOT, AND, OR and XOR with either operator spelling.

    XOR combines sums of products; no Python eval or converter code is used.
    Unknown names and unconsumed text are errors, not assumed zeroes.
    """
    definitions = definitions or {}
    tokens = re.findall(r":\+:|[A-Za-z_][\w.]*|[/!*&+#$()]", expression)
    if "".join(tokens) != re.sub(r"\s", "", expression):
        raise AssertionError(f"invalid Boolean expression: {expression}")
    position = 0

    def peek():
        return tokens[position] if position < len(tokens) else None

    def take():
        nonlocal position
        token = peek()
        if token is None:
            raise AssertionError("incomplete Boolean expression")
        position += 1
        return token

    def atom():
        token = take()
        if token in ("/", "!"):
            return not atom()
        if token == "(":
            value = xor()
            if take() != ")":
                raise AssertionError("unclosed parentheses")
            return value
        if token in ("gnd", "vcc"):
            return token == "vcc"
        if token in values:
            return bool(values[token])
        if token in definitions:
            remaining = {key: value for key, value in definitions.items() if key != token}
            return evaluate(definitions[token], values, remaining)
        raise AssertionError(f"unknown signal {token}")

    def product():
        value = atom()
        while peek() in ("*", "&"):
            take()
            value = atom() & value
        return value

    def sum_of_products():
        value = product()
        while peek() in ("+", "#"):
            take()
            value = product() | value
        return value

    def xor():
        value = sum_of_products()
        while peek() in (":+:", "$"):
            take()
            value = sum_of_products() ^ value
        return value

    value = xor()
    if position != len(tokens):
        raise AssertionError(f"unconsumed tokens: {tokens[position:]}")
    return bool(value)


def definitions(text):
    return dict(re.findall(r'^@define (\w+) "([^"\n]*)"$', text, re.MULTILINE))
