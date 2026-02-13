"""Constants and specifications for the AAX format.

AAX files are MPEG-4 containers (ISO 14496-12) with Audible-specific
metadata atoms and AAC-LC or HE-AAC encoded audio.
"""

# MPEG-4 box/atom type identifiers (4-byte ASCII)
FTYP_BOX = b"ftyp"
MOOV_BOX = b"moov"
MDAT_BOX = b"mdat"
MVHD_BOX = b"mvhd"
TRAK_BOX = b"trak"
TKHD_BOX = b"tkhd"
MDIA_BOX = b"mdia"
MDHD_BOX = b"mdhd"
UDTA_BOX = b"udta"
META_BOX = b"meta"
STBL_BOX = b"stbl"
STSD_BOX = b"stsd"
MINF_BOX = b"minf"
CHPL_BOX = b"chpl"  # Chapter list (Nero-style)

# Audible-specific atoms
AAVD_BOX = b"aavd"  # Audible audio data
ADRM_BOX = b"adrm"  # Audible DRM info

# AAX file type brand
AAX_BRAND = b"aax "
AAXC_BRAND = b"aaxc"  # AAX+ (newer format)

# MIME content type
AAX_CONTENT_TYPE = "audio/vnd.audible.aax"
AAXC_CONTENT_TYPE = "audio/vnd.audible.aaxc"

# File signatures - AAX files start with an ftyp box
# The first 4 bytes are the box size, next 4 are "ftyp"
FTYP_OFFSET = 4

# Supported audio codecs within AAX containers
SUPPORTED_CODECS = frozenset({
    "mp4a",  # AAC-LC
    "aavd",  # Audible-specific codec identifier
})

# Supported sample rates (Hz)
SUPPORTED_SAMPLE_RATES = frozenset({
    22050,
    44100,
})

# Supported bit depths
SUPPORTED_BIT_DEPTHS = frozenset({16})

# Common AAX audio profiles
AUDIO_PROFILES = {
    "format_4": {
        "codec": "mp4a",
        "bitrate": 32000,
        "sample_rate": 22050,
        "channels": 1,
    },
    "format_4_enhanced": {
        "codec": "mp4a",
        "bitrate": 64000,
        "sample_rate": 22050,
        "channels": 1,
    },
    "format_4_high": {
        "codec": "mp4a",
        "bitrate": 128000,
        "sample_rate": 44100,
        "channels": 2,
    },
}

# Maximum metadata field lengths (bytes)
MAX_TITLE_LENGTH = 512
MAX_AUTHOR_LENGTH = 256
MAX_NARRATOR_LENGTH = 256
MAX_DESCRIPTION_LENGTH = 4096

# AAX file extension
AAX_EXTENSION = ".aax"
AAXC_EXTENSION = ".aaxc"
