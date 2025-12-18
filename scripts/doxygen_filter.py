import sys
import re

def filter_text(content):
    pattern = re.compile(
        r'(```[\s\S]*?```|`[^`\n]*?`)'
        r'|(\$\$[\s\S]*?\$\$)'
        r'|((?<!\$)\$(?!\$)[^\n$]+?(?<!\$)\$(?!\$))'
        r'|(\\\|)',
        re.MULTILINE
    )

    def replacer(match):
        g_code = match.group(1)
        g_block_math = match.group(2)
        g_inline_math = match.group(3)
        g_escaped_pipe = match.group(4)

        if g_code:
            return g_code
        
        elif g_block_math:
            inner = g_block_math[2:-2]
            return f"\\f[{inner}\\f]"
        
        elif g_inline_math:
            inner = g_inline_math[1:-1]
            return f"\\f${inner}\\f$"
        
        elif g_escaped_pipe:
            return "&#124;"
        
        return match.group(0)

    return pattern.sub(replacer, content)

if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(0)
    
    filename = sys.argv[1]
    try:
        with open(filename, 'r', encoding='utf-8') as f:
            print(filter_text(f.read()))
    except Exception as e:
        sys.stderr.write(str(e))