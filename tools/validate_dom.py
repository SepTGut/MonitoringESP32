import re

html = open('data/index.html', 'r', encoding='utf-8').read()
html_ids = set(re.findall(r'id=["\']([^"\']+)["\']', html))

js = open('data/script.js', 'r', encoding='utf-8').read()
js_ids = set(re.findall(r'\$\([\'"]([^\'"]+)[\'"]\)', js))

missing = js_ids - html_ids
print(f'Total HTML IDs: {len(html_ids)}')
print(f'Total JS queried IDs: {len(js_ids)}')
print(f'Missing IDs in HTML: {missing}')
if missing:
    raise SystemExit(1)
print("100% MATCH: Zero missing IDs!")
