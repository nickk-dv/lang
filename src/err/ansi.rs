pub fn set_color(color: Color) {
    print!("{}", Color::as_ansi_str(color));
}

pub fn reset() {
    print!("\x1B[0m");
}

pub enum Color {
    Black,
    Red,
    Green,
    Yellow,
    Blue,
    Purple,
    Cyan,
    White,
}

impl Color {
    fn as_ansi_str(color: Color) -> &'static str {
        match color {
            Color::Black => "\x1B[0;30m",
            Color::Red => "\x1B[0;31m",
            Color::Green => "\x1B[0;32m",
            Color::Yellow => "\x1B[0;33m",
            Color::Blue => "\x1B[0;34m",
            Color::Purple => "\x1B[0;35m",
            Color::Cyan => "\x1B[0;36m",
            Color::White => "\x1B[0;37m",
        }
    }
}