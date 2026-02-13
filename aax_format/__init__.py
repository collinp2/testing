"""AAX (Audible Enhanced Audio) format support.

Provides parsing, metadata extraction, and validation for AAX audiobook files.
AAX is an MPEG-4 container format with AAC-encoded audio used by Audible/Amazon.
"""

from aax_format.parser import AAXParser
from aax_format.metadata import AAXMetadata, AAXChapter
from aax_format.constants import (
    AAX_BRAND,
    AAX_CONTENT_TYPE,
    SUPPORTED_CODECS,
    SUPPORTED_SAMPLE_RATES,
)

__version__ = "0.1.0"
__all__ = [
    "AAXParser",
    "AAXMetadata",
    "AAXChapter",
    "AAX_BRAND",
    "AAX_CONTENT_TYPE",
    "SUPPORTED_CODECS",
    "SUPPORTED_SAMPLE_RATES",
]
