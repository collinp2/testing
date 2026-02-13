"""AAX file parser.

Parses MPEG-4 box structure to extract metadata from AAX audiobook files.
AAX files use the ISO Base Media File Format (ISO 14496-12) with
Audible-specific extensions.
"""

import struct
from io import BytesIO
from pathlib import Path
from typing import BinaryIO

from aax_format.constants import (
    AAVD_BOX,
    AAX_BRAND,
    AAXC_BRAND,
    ADRM_BOX,
    CHPL_BOX,
    FTYP_BOX,
    FTYP_OFFSET,
    MDAT_BOX,
    MDHD_BOX,
    MDIA_BOX,
    META_BOX,
    MINF_BOX,
    MOOV_BOX,
    MVHD_BOX,
    STBL_BOX,
    STSD_BOX,
    TKHD_BOX,
    TRAK_BOX,
    UDTA_BOX,
)
from aax_format.metadata import AAXChapter, AAXMetadata


class AAXParseError(Exception):
    """Raised when an AAX file cannot be parsed."""


class AAXParser:
    """Parser for AAX (Audible Enhanced Audio) files.

    AAX files are MPEG-4 containers with AAC audio and Audible-specific
    metadata. This parser reads the box/atom structure to extract metadata
    without decrypting or decoding the audio content.

    Usage:
        parser = AAXParser()
        metadata = parser.parse("audiobook.aax")
        print(metadata.title, metadata.author)
        for chapter in metadata.chapters:
            print(chapter.title, chapter.duration_seconds)
    """

    # Container boxes that hold nested boxes
    CONTAINER_BOXES = frozenset({
        MOOV_BOX, TRAK_BOX, MDIA_BOX, MINF_BOX, STBL_BOX, UDTA_BOX,
    })

    def parse(self, source: str | Path | BinaryIO) -> AAXMetadata:
        """Parse an AAX file and return its metadata.

        Args:
            source: File path (str or Path) or a readable binary stream.

        Returns:
            AAXMetadata with extracted information.

        Raises:
            AAXParseError: If the file is not a valid AAX file.
            FileNotFoundError: If the file path does not exist.
        """
        if isinstance(source, (str, Path)):
            path = Path(source)
            if not path.exists():
                raise FileNotFoundError(f"File not found: {path}")
            with open(path, "rb") as f:
                return self._parse_stream(f, file_size=path.stat().st_size)
        else:
            return self._parse_stream(source)

    def parse_bytes(self, data: bytes) -> AAXMetadata:
        """Parse AAX data from a bytes object.

        Args:
            data: Raw bytes of an AAX file.

        Returns:
            AAXMetadata with extracted information.
        """
        return self._parse_stream(BytesIO(data), file_size=len(data))

    @staticmethod
    def is_aax_file(source: str | Path | BinaryIO) -> bool:
        """Check whether a file is an AAX file by inspecting the ftyp box.

        Args:
            source: File path or readable binary stream.

        Returns:
            True if the file has an AAX or AAXC brand in its ftyp box.
        """
        try:
            if isinstance(source, (str, Path)):
                path = Path(source)
                if not path.exists():
                    return False
                with open(path, "rb") as f:
                    return AAXParser._check_ftyp(f)
            else:
                pos = source.tell()
                try:
                    return AAXParser._check_ftyp(source)
                finally:
                    source.seek(pos)
        except Exception:
            return False

    @staticmethod
    def _check_ftyp(stream: BinaryIO) -> bool:
        """Check the ftyp box for AAX brand."""
        header = stream.read(8)
        if len(header) < 8:
            return False
        box_size = struct.unpack(">I", header[:4])[0]
        box_type = header[4:8]
        if box_type != FTYP_BOX:
            return False
        # Read the major brand (next 4 bytes after box header)
        major_brand = stream.read(4)
        if major_brand in (AAX_BRAND, AAXC_BRAND):
            return True
        # Also check compatible brands
        minor_version = stream.read(4)  # skip minor version
        remaining = box_size - 16
        if remaining > 0 and remaining < 1024:
            brands_data = stream.read(remaining)
            for i in range(0, len(brands_data), 4):
                brand = brands_data[i : i + 4]
                if brand in (AAX_BRAND, AAXC_BRAND):
                    return True
        return False

    def _parse_stream(
        self, stream: BinaryIO, file_size: int = 0
    ) -> AAXMetadata:
        """Parse an AAX file from a binary stream."""
        metadata = AAXMetadata(file_size=file_size)

        # Read and validate ftyp box
        self._parse_ftyp(stream, metadata)

        # Parse remaining top-level boxes
        while True:
            box = self._read_box_header(stream)
            if box is None:
                break

            box_type, box_size, data_start = box

            if box_type == MOOV_BOX:
                self._parse_moov(stream, data_start, box_size, metadata)
            else:
                # Skip non-moov boxes (mdat, free, etc.)
                self._skip_box(stream, data_start, box_size)

        return metadata

    def _parse_ftyp(self, stream: BinaryIO, metadata: AAXMetadata) -> None:
        """Parse the ftyp (file type) box."""
        header = stream.read(8)
        if len(header) < 8:
            raise AAXParseError("File too short to contain ftyp box")

        box_size = struct.unpack(">I", header[:4])[0]
        box_type = header[4:8]

        if box_type != FTYP_BOX:
            raise AAXParseError(
                f"Expected ftyp box, got {box_type!r}"
            )

        major_brand = stream.read(4)
        minor_version_raw = stream.read(4)

        if major_brand == AAX_BRAND:
            metadata.brand = "aax"
            metadata.is_aaxc = False
        elif major_brand == AAXC_BRAND:
            metadata.brand = "aaxc"
            metadata.is_aaxc = True
        else:
            # Check compatible brands
            remaining = box_size - 16
            found = False
            if remaining > 0 and remaining < 4096:
                brands_data = stream.read(remaining)
                for i in range(0, len(brands_data), 4):
                    brand = brands_data[i : i + 4]
                    if brand == AAX_BRAND:
                        metadata.brand = "aax"
                        found = True
                        break
                    elif brand == AAXC_BRAND:
                        metadata.brand = "aaxc"
                        metadata.is_aaxc = True
                        found = True
                        break
            if not found:
                raise AAXParseError(
                    f"Not an AAX file: major brand is {major_brand!r}"
                )
            return

        # Skip remaining compatible brands
        remaining = box_size - 16
        if remaining > 0:
            stream.read(remaining)

    def _read_box_header(
        self, stream: BinaryIO
    ) -> tuple[bytes, int, int] | None:
        """Read a box header and return (type, total_size, data_start).

        Returns None at end of stream.
        """
        pos = stream.tell()
        header = stream.read(8)
        if len(header) < 8:
            return None

        box_size = struct.unpack(">I", header[:4])[0]
        box_type = header[4:8]

        if box_size == 0:
            # Box extends to end of file
            current = stream.tell()
            stream.seek(0, 2)
            end = stream.tell()
            stream.seek(current)
            box_size = end - pos
        elif box_size == 1:
            # 64-bit extended size
            ext = stream.read(8)
            if len(ext) < 8:
                return None
            box_size = struct.unpack(">Q", ext)[0]

        data_start = stream.tell()
        return box_type, box_size, data_start

    def _skip_box(
        self, stream: BinaryIO, data_start: int, box_size: int
    ) -> None:
        """Skip past a box's data."""
        header_size = stream.tell() - data_start
        # data_start is after the header, so we need the original box start
        box_start = data_start - 8  # approximate; header is 8 or 16 bytes
        end = data_start + (box_size - (data_start - box_start + 8) + 8)
        # Simpler: just seek from data_start
        remaining = box_size - (stream.tell() - (data_start - 8))
        if remaining > 0:
            stream.seek(remaining, 1)

    def _parse_moov(
        self,
        stream: BinaryIO,
        data_start: int,
        box_size: int,
        metadata: AAXMetadata,
    ) -> None:
        """Parse the moov (movie) container box."""
        box_start = data_start - 8
        end = box_start + box_size

        while stream.tell() < end:
            box = self._read_box_header(stream)
            if box is None:
                break

            child_type, child_size, child_data_start = box
            child_start = child_data_start - 8
            child_end = child_start + child_size

            if child_type == MVHD_BOX:
                self._parse_mvhd(stream, child_size, metadata)
            elif child_type == TRAK_BOX:
                self._parse_trak(
                    stream, child_data_start, child_size, metadata
                )
            elif child_type == UDTA_BOX:
                self._parse_udta(
                    stream, child_data_start, child_size, metadata
                )

            # Always seek to end of child box to ensure proper alignment
            stream.seek(child_end)

    def _parse_mvhd(
        self, stream: BinaryIO, box_size: int, metadata: AAXMetadata
    ) -> None:
        """Parse the mvhd (movie header) box for duration."""
        version = struct.unpack(">B", stream.read(1))[0]
        stream.read(3)  # flags

        if version == 0:
            _creation = struct.unpack(">I", stream.read(4))[0]
            _modification = struct.unpack(">I", stream.read(4))[0]
            timescale = struct.unpack(">I", stream.read(4))[0]
            duration = struct.unpack(">I", stream.read(4))[0]
        elif version == 1:
            _creation = struct.unpack(">Q", stream.read(8))[0]
            _modification = struct.unpack(">Q", stream.read(8))[0]
            timescale = struct.unpack(">I", stream.read(4))[0]
            duration = struct.unpack(">Q", stream.read(8))[0]
        else:
            return

        if timescale > 0:
            metadata.duration_ms = int((duration / timescale) * 1000)

    def _parse_trak(
        self,
        stream: BinaryIO,
        data_start: int,
        box_size: int,
        metadata: AAXMetadata,
    ) -> None:
        """Parse a trak (track) box to extract audio properties."""
        box_start = data_start - 8
        end = box_start + box_size

        while stream.tell() < end:
            box = self._read_box_header(stream)
            if box is None:
                break

            child_type, child_size, child_data_start = box
            child_start = child_data_start - 8
            child_end = child_start + child_size

            if child_type == MDIA_BOX:
                self._parse_mdia(
                    stream, child_data_start, child_size, metadata
                )
            elif child_type == TKHD_BOX:
                self._parse_tkhd(stream, child_size, metadata)

            stream.seek(child_end)

    def _parse_tkhd(
        self, stream: BinaryIO, box_size: int, metadata: AAXMetadata
    ) -> None:
        """Parse track header to detect audio tracks."""
        start = stream.tell()
        version = struct.unpack(">B", stream.read(1))[0]
        stream.read(3)  # flags

        if version == 0:
            stream.read(4 + 4 + 4 + 4 + 4)  # creation, mod, track_id, reserved, duration
        else:
            stream.read(8 + 8 + 4 + 4 + 8)

        # Skip to volume field (after reserved, layer, alt_group)
        stream.read(2 + 2 + 2 + 2)  # reserved, layer, alt_group, volume

        # Seek to end of tkhd
        remaining = box_size - (stream.tell() - start) - 8
        if remaining > 0:
            stream.read(remaining)

    def _parse_mdia(
        self,
        stream: BinaryIO,
        data_start: int,
        box_size: int,
        metadata: AAXMetadata,
    ) -> None:
        """Parse the mdia (media) container box."""
        box_start = data_start - 8
        end = box_start + box_size

        while stream.tell() < end:
            box = self._read_box_header(stream)
            if box is None:
                break

            child_type, child_size, child_data_start = box
            child_start = child_data_start - 8
            child_end = child_start + child_size

            if child_type == MDHD_BOX:
                self._parse_mdhd(stream, child_size, metadata)
            elif child_type == MINF_BOX:
                self._parse_minf(
                    stream, child_data_start, child_size, metadata
                )

            stream.seek(child_end)

    def _parse_mdhd(
        self, stream: BinaryIO, box_size: int, metadata: AAXMetadata
    ) -> None:
        """Parse media header box for sample rate."""
        version = struct.unpack(">B", stream.read(1))[0]
        stream.read(3)  # flags

        if version == 0:
            stream.read(4 + 4)  # creation, modification
            timescale = struct.unpack(">I", stream.read(4))[0]
            stream.read(4)  # duration
        elif version == 1:
            stream.read(8 + 8)  # creation, modification
            timescale = struct.unpack(">I", stream.read(4))[0]
            stream.read(8)  # duration
        else:
            return

        # For audio tracks, the timescale typically equals the sample rate
        if timescale in (22050, 44100, 48000):
            metadata.sample_rate = timescale

    def _parse_minf(
        self,
        stream: BinaryIO,
        data_start: int,
        box_size: int,
        metadata: AAXMetadata,
    ) -> None:
        """Parse the minf (media information) container box."""
        box_start = data_start - 8
        end = box_start + box_size

        while stream.tell() < end:
            box = self._read_box_header(stream)
            if box is None:
                break

            child_type, child_size, child_data_start = box
            child_start = child_data_start - 8
            child_end = child_start + child_size

            if child_type == STBL_BOX:
                self._parse_stbl(
                    stream, child_data_start, child_size, metadata
                )

            stream.seek(child_end)

    def _parse_stbl(
        self,
        stream: BinaryIO,
        data_start: int,
        box_size: int,
        metadata: AAXMetadata,
    ) -> None:
        """Parse the stbl (sample table) box for codec info."""
        box_start = data_start - 8
        end = box_start + box_size

        while stream.tell() < end:
            box = self._read_box_header(stream)
            if box is None:
                break

            child_type, child_size, child_data_start = box
            child_start = child_data_start - 8
            child_end = child_start + child_size

            if child_type == STSD_BOX:
                self._parse_stsd(stream, child_size, metadata)

            stream.seek(child_end)

    def _parse_stsd(
        self, stream: BinaryIO, box_size: int, metadata: AAXMetadata
    ) -> None:
        """Parse sample description box for codec details."""
        start = stream.tell()
        version = struct.unpack(">B", stream.read(1))[0]
        stream.read(3)  # flags
        entry_count = struct.unpack(">I", stream.read(4))[0]

        if entry_count > 0:
            # Read first entry header
            entry_header = stream.read(8)
            if len(entry_header) >= 8:
                entry_size = struct.unpack(">I", entry_header[:4])[0]
                entry_type = entry_header[4:8]
                codec = entry_type.decode("ascii", errors="replace")
                metadata.codec = codec.strip()

                # For mp4a entries, skip to channel/samplerate fields
                if entry_type == b"mp4a":
                    stream.read(6 + 2)  # reserved, data_ref_index
                    stream.read(8)  # reserved
                    channels = struct.unpack(">H", stream.read(2))[0]
                    metadata.channels = channels
                    stream.read(2 + 2)  # sample_size, compression_id
                    stream.read(2)  # packet_size
                    sr = struct.unpack(">I", stream.read(4))[0]
                    metadata.sample_rate = sr >> 16  # fixed-point 16.16

        # Seek past remaining stsd data
        remaining = box_size - (stream.tell() - start) - 8
        if remaining > 0:
            stream.seek(remaining, 1)

    def _parse_udta(
        self,
        stream: BinaryIO,
        data_start: int,
        box_size: int,
        metadata: AAXMetadata,
    ) -> None:
        """Parse user data box for Audible metadata and chapters."""
        box_start = data_start - 8
        end = box_start + box_size

        while stream.tell() < end:
            box = self._read_box_header(stream)
            if box is None:
                break

            child_type, child_size, child_data_start = box
            child_start = child_data_start - 8
            child_end = child_start + child_size

            if child_type == META_BOX:
                self._parse_meta(
                    stream, child_data_start, child_size, metadata
                )
            elif child_type == CHPL_BOX:
                self._parse_chpl(stream, child_size, metadata)
            elif child_type == ADRM_BOX:
                metadata.has_drm = True
                metadata.drm_type = "audible"

            stream.seek(child_end)

    def _parse_meta(
        self,
        stream: BinaryIO,
        data_start: int,
        box_size: int,
        metadata: AAXMetadata,
    ) -> None:
        """Parse the meta box for iTunes/Audible-style metadata tags."""
        # meta box has a version/flags field
        stream.read(4)  # version + flags

        box_start = data_start - 8
        end = box_start + box_size

        while stream.tell() < end:
            box = self._read_box_header(stream)
            if box is None:
                break

            child_type, child_size, child_data_start = box
            child_start = child_data_start - 8
            child_end = child_start + child_size

            if child_type == b"ilst":
                self._parse_ilst(
                    stream, child_data_start, child_size, metadata
                )

            stream.seek(child_end)

    def _parse_ilst(
        self,
        stream: BinaryIO,
        data_start: int,
        box_size: int,
        metadata: AAXMetadata,
    ) -> None:
        """Parse iTunes metadata list for standard metadata fields."""
        box_start = data_start - 8
        end = box_start + box_size

        while stream.tell() < end:
            box = self._read_box_header(stream)
            if box is None:
                break

            child_type, child_size, child_data_start = box
            child_start = child_data_start - 8

            value = self._read_ilst_value(stream, child_data_start, child_size)
            if value is None:
                stream.seek(child_start + child_size)
                continue

            # Map known iTunes/Audible atoms to metadata fields
            tag = child_type.decode("ascii", errors="replace").strip("\xa9").strip()
            if child_type == b"\xa9nam":
                metadata.title = value
            elif child_type == b"\xa9ART":
                metadata.author = value
            elif child_type == b"\xa9wrt":
                metadata.narrator = value
            elif child_type == b"\xa9cmt":
                metadata.description = value
            elif child_type == b"cprt" or child_type == b"\xa9day":
                metadata.copyright = value
            elif child_type == b"asin":
                metadata.asin = value

            stream.seek(child_start + child_size)

    def _read_ilst_value(
        self, stream: BinaryIO, data_start: int, box_size: int
    ) -> str | None:
        """Read a string value from an ilst entry's data box."""
        box = self._read_box_header(stream)
        if box is None:
            return None

        child_type, child_size, child_data_start = box
        if child_type != b"data":
            return None

        # data box: 4 bytes type indicator + 4 bytes locale
        stream.read(8)
        value_len = child_size - 16  # header(8) + type(4) + locale(4)
        if value_len <= 0 or value_len > 8192:
            return None

        raw = stream.read(value_len)
        try:
            return raw.decode("utf-8")
        except UnicodeDecodeError:
            return raw.decode("latin-1")

    def _parse_chpl(
        self, stream: BinaryIO, box_size: int, metadata: AAXMetadata
    ) -> None:
        """Parse Nero-style chapter list."""
        start = stream.tell()
        # version + flags
        stream.read(4)
        # reserved
        stream.read(1)
        chapter_count = struct.unpack(">I", stream.read(4))[0]

        for _ in range(chapter_count):
            if stream.tell() - start + 8 >= box_size:
                break
            # Timestamp in 100ns units
            timestamp = struct.unpack(">Q", stream.read(8))[0]
            start_ms = timestamp // 10000

            # Chapter title (pascal-style: 1-byte length prefix)
            name_len = struct.unpack(">B", stream.read(1))[0]
            name = stream.read(name_len).decode("utf-8", errors="replace")

            # Calculate duration later (need all chapters first)
            metadata.chapters.append(
                AAXChapter(title=name, start_ms=start_ms, duration_ms=0)
            )

        # Calculate durations from start times
        for i, chapter in enumerate(metadata.chapters):
            if i + 1 < len(metadata.chapters):
                chapter.duration_ms = (
                    metadata.chapters[i + 1].start_ms - chapter.start_ms
                )
            else:
                # Last chapter: duration extends to end of file
                chapter.duration_ms = max(
                    0, metadata.duration_ms - chapter.start_ms
                )
