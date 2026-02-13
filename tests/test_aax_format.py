"""Tests for the AAX format parser."""

import struct
from io import BytesIO

import pytest

from aax_format.constants import (
    AAX_BRAND,
    AAX_CONTENT_TYPE,
    AAXC_BRAND,
    AUDIO_PROFILES,
    SUPPORTED_CODECS,
    SUPPORTED_SAMPLE_RATES,
)
from aax_format.metadata import AAXChapter, AAXMetadata
from aax_format.parser import AAXParser, AAXParseError


# --- Helpers to build minimal AAX files for testing ---


def _build_box(box_type: bytes, payload: bytes) -> bytes:
    """Build an MPEG-4 box with the given type and payload."""
    size = 8 + len(payload)
    return struct.pack(">I", size) + box_type + payload


def _build_ftyp(brand: bytes = AAX_BRAND) -> bytes:
    """Build a minimal ftyp box."""
    minor_version = struct.pack(">I", 0)
    # major brand + minor version + one compatible brand
    payload = brand + minor_version + brand
    return _build_box(b"ftyp", payload)


def _build_mvhd(duration_ms: int = 360000, timescale: int = 1000) -> bytes:
    """Build a minimal mvhd box (version 0)."""
    duration = int(duration_ms * timescale / 1000)
    payload = (
        struct.pack(">B", 0)  # version
        + b"\x00\x00\x00"  # flags
        + struct.pack(">I", 0)  # creation time
        + struct.pack(">I", 0)  # modification time
        + struct.pack(">I", timescale)
        + struct.pack(">I", duration)
    )
    # Pad remaining mvhd fields (rate, volume, matrix, etc.)
    payload += b"\x00" * 80
    return _build_box(b"mvhd", payload)


def _build_chpl(chapters: list[tuple[str, int]]) -> bytes:
    """Build a chapter list box.

    chapters: list of (title, start_ms) tuples.
    """
    payload = (
        b"\x00\x00\x00\x00"  # version + flags
        + b"\x00"  # reserved
        + struct.pack(">I", len(chapters))
    )
    for title, start_ms in chapters:
        # Timestamp in 100ns units
        timestamp = start_ms * 10000
        encoded = title.encode("utf-8")
        payload += struct.pack(">Q", timestamp)
        payload += struct.pack(">B", len(encoded))
        payload += encoded
    return _build_box(b"chpl", payload)


def _build_udta(inner_boxes: bytes = b"") -> bytes:
    """Build a udta box."""
    return _build_box(b"udta", inner_boxes)


def _build_moov(
    duration_ms: int = 360000,
    chapters: list[tuple[str, int]] | None = None,
    extra_udta: bytes = b"",
) -> bytes:
    """Build a minimal moov box."""
    inner = _build_mvhd(duration_ms)
    if chapters is not None or extra_udta:
        udta_inner = b""
        if chapters is not None:
            udta_inner += _build_chpl(chapters)
        udta_inner += extra_udta
        inner += _build_udta(udta_inner)
    return _build_box(b"moov", inner)


def _build_aax_file(
    brand: bytes = AAX_BRAND,
    duration_ms: int = 360000,
    chapters: list[tuple[str, int]] | None = None,
    mdat: bytes = b"\x00" * 100,
) -> bytes:
    """Build a minimal synthetic AAX file."""
    ftyp = _build_ftyp(brand)
    moov = _build_moov(duration_ms, chapters)
    mdat_box = _build_box(b"mdat", mdat)
    return ftyp + moov + mdat_box


# --- Tests ---


class TestAAXMetadata:
    def test_default_values(self):
        meta = AAXMetadata()
        assert meta.title == ""
        assert meta.author == ""
        assert meta.duration_ms == 0
        assert meta.chapters == []
        assert meta.has_drm is False

    def test_duration_seconds(self):
        meta = AAXMetadata(duration_ms=7200000)
        assert meta.duration_seconds == 7200.0

    def test_duration_formatted(self):
        meta = AAXMetadata(duration_ms=7384000)  # 2h 3m 4s
        assert meta.duration_formatted == "02:03:04"

    def test_duration_formatted_zero(self):
        meta = AAXMetadata(duration_ms=0)
        assert meta.duration_formatted == "00:00:00"

    def test_chapter_count(self):
        meta = AAXMetadata(
            chapters=[
                AAXChapter("Ch 1", 0, 60000),
                AAXChapter("Ch 2", 60000, 60000),
            ]
        )
        assert meta.chapter_count == 2

    def test_to_dict(self):
        meta = AAXMetadata(
            title="Test Book",
            author="Author Name",
            duration_ms=120000,
        )
        d = meta.to_dict()
        assert d["title"] == "Test Book"
        assert d["author"] == "Author Name"
        assert d["duration_ms"] == 120000
        assert d["duration_formatted"] == "00:02:00"
        assert d["chapter_count"] == 0
        assert isinstance(d["chapters"], list)

    def test_to_dict_with_chapters(self):
        ch = AAXChapter("Intro", 0, 30000)
        meta = AAXMetadata(chapters=[ch])
        d = meta.to_dict()
        assert len(d["chapters"]) == 1
        assert d["chapters"][0]["title"] == "Intro"
        assert d["chapters"][0]["start_ms"] == 0


class TestAAXChapter:
    def test_properties(self):
        ch = AAXChapter("Chapter 1", start_ms=5000, duration_ms=120000)
        assert ch.end_ms == 125000
        assert ch.start_seconds == 5.0
        assert ch.duration_seconds == 120.0

    def test_repr(self):
        ch = AAXChapter("Test", 1000, 2000)
        r = repr(ch)
        assert "Test" in r
        assert "1.0s" in r
        assert "2.0s" in r


class TestAAXParser:
    def test_parse_minimal_aax(self):
        data = _build_aax_file()
        parser = AAXParser()
        meta = parser.parse_bytes(data)
        assert meta.brand == "aax"
        assert meta.is_aaxc is False
        assert meta.duration_ms == 360000

    def test_parse_aaxc_brand(self):
        data = _build_aax_file(brand=AAXC_BRAND)
        parser = AAXParser()
        meta = parser.parse_bytes(data)
        assert meta.brand == "aaxc"
        assert meta.is_aaxc is True

    def test_parse_duration(self):
        data = _build_aax_file(duration_ms=7200000)
        parser = AAXParser()
        meta = parser.parse_bytes(data)
        assert meta.duration_ms == 7200000

    def test_parse_chapters(self):
        chapters = [
            ("Opening Credits", 0),
            ("Chapter 1: The Beginning", 30000),
            ("Chapter 2: The Middle", 180000),
            ("Chapter 3: The End", 300000),
        ]
        data = _build_aax_file(duration_ms=360000, chapters=chapters)
        parser = AAXParser()
        meta = parser.parse_bytes(data)

        assert meta.chapter_count == 4
        assert meta.chapters[0].title == "Opening Credits"
        assert meta.chapters[0].start_ms == 0
        assert meta.chapters[0].duration_ms == 30000
        assert meta.chapters[1].title == "Chapter 1: The Beginning"
        assert meta.chapters[1].start_ms == 30000
        assert meta.chapters[1].duration_ms == 150000
        # Last chapter extends to end
        assert meta.chapters[3].start_ms == 300000
        assert meta.chapters[3].duration_ms == 60000  # 360000 - 300000

    def test_parse_empty_chapters(self):
        data = _build_aax_file(chapters=[])
        parser = AAXParser()
        meta = parser.parse_bytes(data)
        assert meta.chapter_count == 0

    def test_is_aax_file_true(self):
        data = _build_aax_file()
        assert AAXParser.is_aax_file(BytesIO(data)) is True

    def test_is_aax_file_false(self):
        data = b"\x00\x00\x00\x08noax" + b"\x00" * 100
        assert AAXParser.is_aax_file(BytesIO(data)) is False

    def test_is_aax_file_empty(self):
        assert AAXParser.is_aax_file(BytesIO(b"")) is False

    def test_is_aax_file_too_short(self):
        assert AAXParser.is_aax_file(BytesIO(b"\x00\x01")) is False

    def test_parse_invalid_no_ftyp(self):
        data = b"\x00\x00\x00\x08moov" + b"\x00" * 100
        parser = AAXParser()
        with pytest.raises(AAXParseError, match="Expected ftyp"):
            parser.parse_bytes(data)

    def test_parse_invalid_wrong_brand(self):
        ftyp_payload = b"isom" + b"\x00" * 8
        data = _build_box(b"ftyp", ftyp_payload) + b"\x00" * 100
        parser = AAXParser()
        with pytest.raises(AAXParseError, match="Not an AAX file"):
            parser.parse_bytes(data)

    def test_parse_too_short(self):
        parser = AAXParser()
        with pytest.raises(AAXParseError, match="too short"):
            parser.parse_bytes(b"\x00\x00")

    def test_file_size_recorded(self):
        data = _build_aax_file()
        parser = AAXParser()
        meta = parser.parse_bytes(data)
        assert meta.file_size == len(data)

    def test_is_aax_preserves_stream_position(self):
        data = _build_aax_file()
        stream = BytesIO(data)
        stream.seek(10)
        AAXParser.is_aax_file(stream)
        assert stream.tell() == 10

    def test_parse_file_not_found(self, tmp_path):
        parser = AAXParser()
        with pytest.raises(FileNotFoundError):
            parser.parse(tmp_path / "nonexistent.aax")

    def test_parse_from_path(self, tmp_path):
        data = _build_aax_file()
        path = tmp_path / "test.aax"
        path.write_bytes(data)
        parser = AAXParser()
        meta = parser.parse(path)
        assert meta.brand == "aax"
        assert meta.duration_ms == 360000

    def test_parse_from_string_path(self, tmp_path):
        data = _build_aax_file()
        path = tmp_path / "test.aax"
        path.write_bytes(data)
        parser = AAXParser()
        meta = parser.parse(str(path))
        assert meta.brand == "aax"


class TestConstants:
    def test_aax_brand(self):
        assert AAX_BRAND == b"aax "
        assert len(AAX_BRAND) == 4

    def test_content_type(self):
        assert AAX_CONTENT_TYPE == "audio/vnd.audible.aax"

    def test_supported_codecs(self):
        assert "mp4a" in SUPPORTED_CODECS
        assert "aavd" in SUPPORTED_CODECS

    def test_supported_sample_rates(self):
        assert 22050 in SUPPORTED_SAMPLE_RATES
        assert 44100 in SUPPORTED_SAMPLE_RATES

    def test_audio_profiles(self):
        assert "format_4" in AUDIO_PROFILES
        assert AUDIO_PROFILES["format_4"]["codec"] == "mp4a"
        assert AUDIO_PROFILES["format_4"]["sample_rate"] == 22050
