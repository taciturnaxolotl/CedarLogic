#!/bin/bash
# Update the Sparkle/WinSparkle appcast.xml with a new release
# Usage: ./update-appcast.sh VERSION TAG DMG_SIZE EXE_SIZE SPARKLE_SIGNATURE RELEASE_NOTES REPO

set -e

VERSION="$1"
TAG="$2"
DMG_SIZE="$3"
EXE_SIZE="$4"
SPARKLE_SIGNATURE="$5"
RELEASE_NOTES="$6"
REPO="$7"

PUBDATE=$(date -R)
DMG_URL="https://github.com/${REPO}/releases/download/${TAG}/CedarLogic-${VERSION}-Darwin.dmg"
EXE_URL="https://github.com/${REPO}/releases/download/${TAG}/CedarLogic-${VERSION}-win32.exe"

# Convert markdown release notes to HTML
RELEASE_NOTES_HTML=$(echo "$RELEASE_NOTES" | python3 -c "
import sys, re, html
md = sys.stdin.read().strip()
# Convert markdown to basic HTML
lines = []
in_list = False
for line in md.split('\n'):
    stripped = line.strip()
    if stripped.startswith('### '):
        if in_list: lines.append('</ul>'); in_list = False
        lines.append(f'<h3>{html.escape(stripped[4:])}</h3>')
    elif stripped.startswith('## '):
        if in_list: lines.append('</ul>'); in_list = False
        lines.append(f'<h2>{html.escape(stripped[3:])}</h2>')
    elif stripped.startswith('- '):
        if not in_list: lines.append('<ul>'); in_list = True
        content = stripped[2:]
        content = re.sub(r'\*\*(.+?)\*\*', r'<b>\1</b>', content)
        content = re.sub(r'\`(.+?)\`', r'<code>\1</code>', content)
        lines.append(f'  <li>{content}</li>')
    elif stripped == '':
        if in_list: lines.append('</ul>'); in_list = False
    else:
        if in_list: lines.append('</ul>'); in_list = False
        content = re.sub(r'\*\*(.+?)\*\*', r'<b>\1</b>', stripped)
        lines.append(f'<p>{content}</p>')
if in_list: lines.append('</ul>')
print('\n'.join(lines))
")

cat > docs/appcast.xml << APPCAST_EOF
<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle" xmlns:dc="http://purl.org/dc/elements/1.1/">
  <channel>
    <title>CedarLogic Updates</title>
    <link>https://taciturnaxolotl.github.io/CedarLogic/appcast.xml</link>
    <description>Most recent updates to CedarLogic</description>
    <language>en</language>
    <!-- macOS -->
    <item>
      <title>Version ${VERSION}</title>
      <pubDate>${PUBDATE}</pubDate>
      <sparkle:version>${VERSION}</sparkle:version>
      <sparkle:shortVersionString>${VERSION}</sparkle:shortVersionString>
      <sparkle:minimumSystemVersion>11.0</sparkle:minimumSystemVersion>
      <description><![CDATA[${RELEASE_NOTES_HTML}]]></description>
      <enclosure
        url="${DMG_URL}"
        sparkle:edSignature="${SPARKLE_SIGNATURE}"
        length="${DMG_SIZE}"
        type="application/octet-stream"
        sparkle:os="macos"/>
    </item>
    <!-- Windows -->
    <item>
      <title>Version ${VERSION}</title>
      <pubDate>${PUBDATE}</pubDate>
      <sparkle:version>${VERSION}</sparkle:version>
      <sparkle:shortVersionString>${VERSION}</sparkle:shortVersionString>
      <description><![CDATA[${RELEASE_NOTES_HTML}]]></description>
      <enclosure
        url="${EXE_URL}"
        length="${EXE_SIZE}"
        type="application/octet-stream"
        sparkle:os="windows"
        sparkle:installerArguments="/S"/>
    </item>
  </channel>
</rss>
APPCAST_EOF

echo "Appcast updated for version ${VERSION}"
