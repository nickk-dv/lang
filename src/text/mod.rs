pub mod find;

#[derive(Copy, Clone)]
pub struct TextRange {
    start: TextOffset,
    end: TextOffset,
}

#[derive(Copy, Clone, PartialEq, Eq, PartialOrd, Ord)]
pub struct TextOffset {
    raw: u32,
}

#[derive(Copy, Clone)]
pub struct TextLocation {
    line: u32,
    col: u32,
}

impl TextRange {
    #[inline]
    pub const fn new(start: TextOffset, end: TextOffset) -> TextRange {
        assert!(start.raw <= end.raw);
        TextRange { start, end }
    }
    #[inline]
    pub const fn empty_at(offset: TextOffset) -> TextRange {
        TextRange {
            start: offset,
            end: offset,
        }
    }
    #[inline]
    pub const fn start(self) -> TextOffset {
        self.start
    }
    #[inline]
    pub const fn end(self) -> TextOffset {
        self.end
    }
    #[inline]
    pub const fn len(self) -> usize {
        (self.end.raw - self.start.raw) as usize
    }
    #[inline]
    pub fn extend_by(&mut self, by: TextOffset) {
        self.end += by;
    }
    #[inline]
    pub const fn contains_offset(self, offset: TextOffset) -> bool {
        self.start.raw <= offset.raw && offset.raw < self.end.raw
    }
    #[inline]
    pub fn as_usize(self) -> std::ops::Range<usize> {
        self.start.into()..self.end.into()
    }
}

impl TextOffset {
    #[inline]
    pub const fn new(offset: u32) -> TextOffset {
        TextOffset { raw: offset }
    }
}

impl From<u32> for TextOffset {
    #[inline]
    fn from(value: u32) -> Self {
        TextOffset { raw: value }
    }
}

impl From<TextOffset> for u32 {
    #[inline]
    fn from(value: TextOffset) -> Self {
        value.raw
    }
}

impl From<TextOffset> for usize {
    #[inline]
    fn from(value: TextOffset) -> Self {
        value.raw as usize
    }
}

impl std::ops::Add for TextOffset {
    type Output = TextOffset;
    #[inline]
    fn add(self, rhs: TextOffset) -> Self::Output {
        (self.raw + rhs.raw).into()
    }
}

impl std::ops::AddAssign for TextOffset {
    #[inline]
    fn add_assign(&mut self, rhs: TextOffset) {
        self.raw = self.raw + rhs.raw;
    }
}

impl TextLocation {
    #[inline]
    pub const fn new(line: u32, col: u32) -> TextLocation {
        TextLocation { line, col }
    }
    #[inline]
    pub const fn line(&self) -> u32 {
        self.line
    }
    #[inline]
    pub const fn col(&self) -> u32 {
        self.col
    }
}

use std::fmt;

impl fmt::Debug for TextRange {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}..{}", self.start.raw, self.end.raw)
    }
}

impl fmt::Debug for TextOffset {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.raw)
    }
}

impl fmt::Debug for TextLocation {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}:{}", self.line, self.col)
    }
}
