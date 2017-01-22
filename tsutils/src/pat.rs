extern crate std;

#[derive(Debug)]
pub struct ProgramAssociationTable {
    pub table_id: u8,
    pub transport_stream_id: u16,
    pub version_number: u8,
    pub current_next_indicator: bool,
    pub section_number: u8,
    pub last_section_number: u8,
    pub program_map: std::collections::HashMap<u16, u16>,
    pub crc32: u32,
}

impl ProgramAssociationTable {
    pub fn parse(payload: &[u8]) -> Result<Self, super::psi::ParseError> {
        // ISO/IEC 13818-1 2.4.4.1 Table 2-29
        // ISO/IEC 13818-1 2.4.4.2
        let pointer_field = payload[0] as usize;
        let payload = &payload[(1 + pointer_field)..];

        // ISO/IEC 13818-1 2.4.4.3 Table 2-30
        // ISO/IEC 13818-1 2.4.4.4
        let table_id = payload[0];
        if table_id != 0x00 {
            return Err(super::psi::ParseError::IncorrectTableId {
                expected: 0x00,
                actual: table_id,
            });
        }

        // ISO/IEC 13818-1 2.4.4.5
        let section_syntax_indicator = (payload[1] & 0b10000000) != 0;
        if !section_syntax_indicator {
            return Err(super::psi::ParseError::IncorrectSectionSyntaxIndicator);
        }
        let section_length = ((payload[1] & 0b00001111) as usize) << 8 | payload[2] as usize;
        let transport_stream_id = ((payload[3] as u16) << 8) | payload[4] as u16;
        let version_number = (payload[5] & 0b00111110) >> 1;
        let current_next_indicator = (payload[5] & 0b00000001) != 0;
        let section_number = payload[6];
        let last_section_number = payload[7];

        let n = (section_length - 5) / 4;
        let mut program_map = std::collections::HashMap::new();
        for i in 0..n {
            let index = 8 + i * 4;
            let program_number = (payload[index] as u16) << 8 | payload[index + 1] as u16;
            let pid = ((payload[index + 2] & 0b00011111) as u16) << 8 | payload[index + 3] as u16;
            if program_number == 0 {
                // Network_PID
            } else {
                program_map.insert(pid, program_number);
            }
        }
        let index = 8 + n * 4;
        let crc32 = (payload[index] as u32) << 24 | (payload[index + 1] as u32) << 16 |
                    (payload[index + 2] as u32) << 8 |
                    payload[index + 3] as u32;

        Ok(ProgramAssociationTable {
            table_id: table_id,
            transport_stream_id: transport_stream_id,
            version_number: version_number,
            current_next_indicator: current_next_indicator,
            section_number: section_number,
            last_section_number: last_section_number,
            program_map: program_map,
            crc32: crc32,
        })
    }
}
