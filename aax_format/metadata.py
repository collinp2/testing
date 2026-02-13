"""AAX metadata structures."""

from dataclasses import dataclass, field


@dataclass
class AAXChapter:
    """Represents a single chapter in an AAX audiobook."""

    title: str
    start_ms: int
    duration_ms: int

    @property
    def end_ms(self) -> int:
        return self.start_ms + self.duration_ms

    @property
    def start_seconds(self) -> float:
        return self.start_ms / 1000.0

    @property
    def duration_seconds(self) -> float:
        return self.duration_ms / 1000.0

    def __repr__(self) -> str:
        return (
            f"AAXChapter(title={self.title!r}, "
            f"start={self.start_seconds:.1f}s, "
            f"duration={self.duration_seconds:.1f}s)"
        )


@dataclass
class AAXMetadata:
    """Metadata extracted from an AAX audiobook file."""

    # File info
    file_size: int = 0
    brand: str = ""
    is_aaxc: bool = False

    # Book metadata
    title: str = ""
    author: str = ""
    narrator: str = ""
    description: str = ""
    copyright: str = ""
    asin: str = ""  # Amazon Standard Identification Number
    content_id: str = ""
    codec_id: str = ""
    audible_product_id: str = ""

    # Audio properties
    duration_ms: int = 0
    sample_rate: int = 0
    channels: int = 0
    bitrate: int = 0
    codec: str = ""

    # Chapter information
    chapters: list[AAXChapter] = field(default_factory=list)

    # DRM info (non-sensitive)
    has_drm: bool = False
    drm_type: str = ""

    @property
    def duration_seconds(self) -> float:
        return self.duration_ms / 1000.0

    @property
    def duration_formatted(self) -> str:
        """Return duration as HH:MM:SS."""
        total_seconds = int(self.duration_seconds)
        hours = total_seconds // 3600
        minutes = (total_seconds % 3600) // 60
        seconds = total_seconds % 60
        return f"{hours:02d}:{minutes:02d}:{seconds:02d}"

    @property
    def chapter_count(self) -> int:
        return len(self.chapters)

    def to_dict(self) -> dict:
        """Serialize metadata to a dictionary."""
        return {
            "file_size": self.file_size,
            "brand": self.brand,
            "is_aaxc": self.is_aaxc,
            "title": self.title,
            "author": self.author,
            "narrator": self.narrator,
            "description": self.description,
            "copyright": self.copyright,
            "asin": self.asin,
            "content_id": self.content_id,
            "audible_product_id": self.audible_product_id,
            "duration_ms": self.duration_ms,
            "duration_formatted": self.duration_formatted,
            "sample_rate": self.sample_rate,
            "channels": self.channels,
            "bitrate": self.bitrate,
            "codec": self.codec,
            "has_drm": self.has_drm,
            "drm_type": self.drm_type,
            "chapter_count": self.chapter_count,
            "chapters": [
                {
                    "title": ch.title,
                    "start_ms": ch.start_ms,
                    "duration_ms": ch.duration_ms,
                }
                for ch in self.chapters
            ],
        }
