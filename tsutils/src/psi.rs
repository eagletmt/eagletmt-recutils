#[derive(Debug)]
pub enum ParseError {
    IncorrectTableId { expected: u8, actual: u8 },
    IncorrectSectionSyntaxIndicator,
}
