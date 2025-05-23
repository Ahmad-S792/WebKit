#!/usr/bin/env python3

import os
import sys
from datetime import datetime, timedelta, timezone
from urllib.parse import parse_qs

query = parse_qs(os.environ.get('QUERY_STRING', ''), keep_blank_values=True)
name = query.get('name', [''])[0]
value = query.get('value', [''])[0]
message = query.get('message', [''])[0]
isPartitioned = query.get('partitioned', [False])[0]
partitioned = ""
if isPartitioned is not None and isPartitioned:
    partitioned = "Partitioned; "

expires = datetime.now(timezone.utc) + timedelta(seconds=60*60*24*30)

sys.stdout.write(
    'Set-Cookie: {name}={value}; expires={expires} GMT; Max-Age=2592000; SameSite=None; Secure; {partitioned}path=/\r\n'
    'Content-Type: text/html\r\n\r\n'
    '{message}<br>'
    '<script>\n'
    'if (document.location.hash) {{\n'
    '    setTimeout("document.location.href = document.location.hash.substring(1)", 10);\n'
    '}}\n'
    '</script>\n'.format(name=name, value=value, expires=expires.strftime('%a, %d-%b-%Y %H:%M:%S'), message=message, partitioned=partitioned)
)
