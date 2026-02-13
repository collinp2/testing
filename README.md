# aax_format

Python library for parsing and extracting metadata from AAX (Audible Enhanced Audio) audiobook files.

AAX is an MPEG-4 container format with AAC-encoded audio used by Audible/Amazon for audiobooks.

## Features

- Parse AAX and AAXC file formats
- Extract book metadata (title, author, narrator, description, ASIN)
- Extract chapter information with timestamps and durations
- Detect DRM presence
- Identify audio properties (codec, sample rate, channels, bitrate)
- File format validation via `is_aax_file()`

## Usage

```python
from aax_format import AAXParser

parser = AAXParser()

# Parse from file path
metadata = parser.parse("audiobook.aax")

# Or from bytes
metadata = parser.parse_bytes(raw_data)

# Access metadata
print(metadata.title)
print(metadata.author)
print(metadata.duration_formatted)  # "02:15:30"

# Iterate chapters
for chapter in metadata.chapters:
    print(f"{chapter.title}: {chapter.duration_seconds:.0f}s")

# Check if a file is AAX
if AAXParser.is_aax_file("unknown.bin"):
    print("This is an AAX file")
```

## Running tests

```
pip install pytest
pytest tests/
```
