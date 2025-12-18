import sys
import re

def filter_text(content):
    content = content.replace(r'\implies', r'\Longrightarrow')

    pattern = re.compile(
        r'(```[\s\S]*?```)'
        r'|(`[^`\n]*?`)'
        r'|(\$\$[\s\S]*?\$\$)'
        r'|((?<!\$)\$(?!\$)[^\n$]+?(?<!\$)\$(?!\$))'
        r'|(\\\|)',
        re.MULTILINE
    )

    def replacer(match):
        g_block_code = match.group(1)
        g_inline_code = match.group(2)
        g_block_math = match.group(3)
        g_inline_math = match.group(4)
        g_escaped_pipe = match.group(5)

        if g_block_code:
            return g_block_code

        if g_inline_code:
            return g_inline_code.replace(r'\|', '&#124;')

        if g_block_math:
            inner = g_block_math[2:-2]
            return f"\\f[{inner}\\f]"
        
        if g_inline_math:
            inner = g_inline_math[1:-1]
            return f"\\f${inner}\\f$"
        
        if g_escaped_pipe:
            return "&#124;"
        
        return match.group(0)

    return pattern.sub(replacer, content)

if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(0)
    
    filename = sys.argv[1]
    try:
        with open(filename, 'r', encoding='utf-8', errors='ignore') as f:
            print(filter_text(f.read()))
    except Exception as e:
        sys.stderr.write(str(e))