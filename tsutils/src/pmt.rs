#[derive(Debug)]
pub struct ProgramMapTable {
    pub table_id: u8,
    pub program_number: u16,
    pub version_number: u8,
    pub current_next_indicator: bool,
    pub section_number: u8,
    pub last_section_number: u8,
    pub pcr_pid: u16,
    pub program_info: Vec<u8>,
    pub es_info: Vec<EsInfo>,
    pub crc32: u32,
}

impl ProgramMapTable {
    pub fn parse(payload: &[u8]) -> Result<Self, super::psi::ParseError> {
        // ISO/IEC 13818-1 2.4.4.1 Table 2-29
        // ISO/IEC 13818-1 2.4.4.2
        let pointer_field = payload[0] as usize;
        let payload = &payload[(1 + pointer_field)..];

        // ISO/IEC 13818-1 2.4.4.8 Table 2-33
        // ISO/IEC 13818-1 2.4.4.9 Table 2-33
        let table_id = payload[0];
        if table_id != 0x02 {
            return Err(super::psi::ParseError::IncorrectTableId {
                expected: 0x02,
                actual: table_id,
            });
        }
        let section_syntax_indicator = (payload[1] & 0b10000000) != 0;
        if !section_syntax_indicator {
            return Err(super::psi::ParseError::IncorrectSectionSyntaxIndicator);
        }
        let section_length = ((payload[1] & 0b00001111) as usize) << 8 | payload[2] as usize;
        let program_number = (payload[3] as u16) << 8 | payload[4] as u16;
        let version_number = (payload[5] & 0b00111110) >> 1;
        let current_next_indicator = (payload[5] & 0b00000001) != 0;
        let section_number = payload[6];
        let last_section_number = payload[7];
        let pcr_pid = ((payload[8] & 0b00011111) as u16) << 8 | payload[9] as u16;
        let program_info_length = ((payload[10] & 0b00001111) as usize) << 8 | payload[11] as usize;
        let program_info = payload[12..(12 + program_info_length)].to_vec();

        let mut index = 12 + program_info_length;
        let mut es_info = vec![];
        while index < 3 + section_length - 4 {
            let info = EsInfo::new(&payload[index..]);
            index += info.size();
            es_info.push(info);
        }
        let crc32 = (payload[3 + section_length - 4] as u32) << 24 |
                    (payload[3 + section_length - 3] as u32) << 16 |
                    (payload[3 + section_length - 2] as u32) << 8 |
                    (payload[3 + section_length - 1] as u32);

        Ok(ProgramMapTable {
            table_id: table_id,
            program_number: program_number,
            version_number: version_number,
            current_next_indicator: current_next_indicator,
            section_number: section_number,
            last_section_number: last_section_number,
            pcr_pid: pcr_pid,
            program_info: program_info,
            es_info: es_info,
            crc32: crc32,
        })
    }
}

#[derive(Debug)]
pub struct EsInfo {
    pub stream_type: u8,
    pub elementary_pid: u16,
    pub descriptor: Vec<u8>,
}

impl EsInfo {
    pub fn new(payload: &[u8]) -> Self {
        let stream_type = payload[0];
        let elementary_pid = ((payload[1] & 0b00011111) as u16) << 8 | payload[2] as u16;
        let es_info_length = ((payload[3] & 0b00001111) as usize) << 8 | payload[4] as usize;
        let descriptor = payload[5..(5 + es_info_length)].to_vec();
        EsInfo {
            stream_type: stream_type,
            elementary_pid: elementary_pid,
            descriptor: descriptor,
        }
    }

    pub fn size(&self) -> usize {
        5 + self.descriptor.len()
    }
}
