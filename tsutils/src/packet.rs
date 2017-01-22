extern crate std;

pub struct TsPackets<R> {
    reader: R,
}

impl<R: std::io::Read> Iterator for TsPackets<R> {
    type Item = Result<[u8; 188], std::io::Error>;

    fn next(&mut self) -> Option<Result<[u8; 188], std::io::Error>> {
        let mut buf = [0; 188];
        match self.reader.read_exact(&mut buf) {
            Ok(()) => Some(Ok(buf)),
            Err(e) => {
                match e.kind() {
                    std::io::ErrorKind::UnexpectedEof => None,
                    _ => Some(Err(e)),
                }
            }
        }
    }
}

pub fn ts_packets<R>(reader: R) -> TsPackets<R> {
    TsPackets { reader: reader }
}

#[derive(Debug)]
pub struct TsPacket<'a> {
    pub sync_byte: u8,
    pub transport_error_indicator: bool,
    pub payload_unit_start_indicator: bool,
    pub transport_priority: bool,
    pub pid: u16,
    pub transport_scrambling_control: u8,
    pub adaptation_field_control: u8,
    pub continuity_counter: u8,
    pub adaptation_field: Option<AdaptationField<'a>>,
    pub data_bytes: Option<&'a [u8]>,
}

impl<'a> TsPacket<'a> {
    pub fn new(packet: &'a [u8]) -> Self {
        // ISO/IEC 13818-1 2.4.3.2 Table 2-2
        // ISO/IEC 13818-1 2.4.3.3
        let sync_byte = packet[0];
        let transport_error_indicator = (packet[1] & 0b10000000) != 0;
        let payload_unit_start_indicator = (packet[1] & 0b01000000) != 0;
        let transport_priority = (packet[1] & 0b00100000) != 0;
        let pid = ((packet[1] & 0b00011111) as u16) << 8 | (packet[2] as u16);
        let transport_scrambling_control = (packet[3] & 0b11000000) >> 6;
        let adaptation_field_control = (packet[3] & 0b00110000) >> 4;
        let continuity_counter = packet[3] & 0b00001111;

        let mut index = 4;

        let adaptation_field = if adaptation_field_control == 0b10 ||
                                  adaptation_field_control == 0b11 {
            let adaptation_field = AdaptationField::parse(&packet[index..]);
            if let Some(ref af) = adaptation_field {
                index += af.adaptation_field_length as usize + 1;
            } else {
                index += 1
            }
            adaptation_field
        } else {
            None
        };

        let data_bytes = if adaptation_field_control == 0b01 || adaptation_field_control == 0b11 {
            Some(&packet[index..])
        } else {
            None
        };

        TsPacket {
            sync_byte: sync_byte,
            transport_error_indicator: transport_error_indicator,
            payload_unit_start_indicator: payload_unit_start_indicator,
            transport_priority: transport_priority,
            pid: pid,
            transport_scrambling_control: transport_scrambling_control,
            adaptation_field_control: adaptation_field_control,
            continuity_counter: continuity_counter,
            adaptation_field: adaptation_field,
            data_bytes: data_bytes,
        }
    }

    pub fn check_sync_byte(&self) -> bool {
        self.sync_byte == 0x47
    }
}

#[derive(Debug)]
pub struct AdaptationField<'a> {
    pub adaptation_field_length: u8,
    pub discontinuity_indicator: bool,
    pub random_access_indicator: bool,
    pub elementary_stream_priority_indicator: bool,
    pub transport_private_data_flag: bool,
    pub pcr: Option<PCR>,
    pub opcr: Option<OPCR>,
    pub splice_countdown: Option<i8>,
    pub transport_private_data: Option<&'a [u8]>,
    pub adaptation_field_extension: Option<AdaptationFieldExtension<'a>>,
}

impl<'a> AdaptationField<'a> {
    fn parse(packet: &'a [u8]) -> Option<Self> {
        // ISO/IEC 13818-1 2.4.3.4 Table 2-6
        // ISO/IEC 13818-1 2.4.3.5
        let adaptation_field_length = packet[0];
        if adaptation_field_length == 0 {
            None
        } else {
            let discontinuity_indicator = (packet[1] & 0b100000) != 0;
            let random_access_indicator = (packet[1] & 0b010000) != 0;
            let elementary_stream_priority_indicator = (packet[1] & 0b00100000) != 0;
            let pcr_flag = (packet[1] & 0b00010000) != 0;
            let opcr_flag = (packet[1] & 0b00001000) != 0;
            let splicing_point_flag = (packet[1] & 0b00000100) != 0;
            let transport_private_data_flag = (packet[1] & 0b00000010) != 0;
            let adaptation_field_extension_flag = (packet[1] & 0b00000001) != 0;

            let mut index = 2;

            let pcr = if pcr_flag {
                let pcr = PCR::new(&packet[index..]);
                index += PCR::size();
                Some(pcr)
            } else {
                None
            };

            let opcr = if opcr_flag {
                let opcr = OPCR::new(&packet[index..]);
                index += OPCR::size();
                Some(opcr)
            } else {
                None
            };

            let splice_countdown = if splicing_point_flag {
                let splice_countdown = packet[index] as i8;
                index += 1;
                Some(splice_countdown)
            } else {
                None
            };

            let transport_private_data = if transport_private_data_flag {
                let length = packet[index] as usize;
                index += 1;
                let data = &packet[index..(index + length)];
                index += length;
                Some(data)
            } else {
                None
            };

            let adaptation_field_extension = if adaptation_field_extension_flag {
                let extension = AdaptationFieldExtension::new(&packet[index..]);
                index += extension.adaptation_field_extension_length as usize;
                Some(extension)
            } else {
                None
            };

            // Check stuffing_bytes
            for &stuffing_byte in &packet[index..(adaptation_field_length as usize + 1)] {
                if stuffing_byte != 0xff {
                    warn!("Invalid stuffing_byte in adaptation field: {}",
                          stuffing_byte);
                }
            }

            Some(AdaptationField {
                adaptation_field_length: adaptation_field_length,
                discontinuity_indicator: discontinuity_indicator,
                random_access_indicator: random_access_indicator,
                elementary_stream_priority_indicator: elementary_stream_priority_indicator,
                transport_private_data_flag: transport_private_data_flag,
                pcr: pcr,
                opcr: opcr,
                splice_countdown: splice_countdown,
                transport_private_data: transport_private_data,
                adaptation_field_extension: adaptation_field_extension,
            })
        }
    }
}

#[derive(Debug)]
pub struct PCR {
    pub program_clock_reference_base: u64,
    pub reserved: u8,
    pub program_clock_reference_extension: u16,
}

impl PCR {
    fn new(packet: &[u8]) -> Self {
        PCR {
            program_clock_reference_base: ((packet[0] as u64) << 32) | ((packet[1] as u64) << 24) |
                                          ((packet[2] as u64) << 16) |
                                          (packet[3] as u64) << 8 |
                                          (packet[4] & 0b10000000) as u64,
            reserved: packet[4] & 0b01111110,
            program_clock_reference_extension: ((packet[4] & 0b00000001) as u16) << 8 |
                                               packet[5] as u16,
        }
    }

    fn size() -> usize {
        6
    }
}

#[derive(Debug)]
pub struct OPCR {
    pub original_program_clock_reference_base: u64,
    pub reserved: u8,
    pub original_program_clock_reference_extension: u16,
}

impl OPCR {
    fn new(packet: &[u8]) -> Self {
        OPCR {
            original_program_clock_reference_base: ((packet[0] as u64) << 32) |
                                                   ((packet[1] as u64) << 24) |
                                                   ((packet[2] as u64) << 16) |
                                                   (packet[3] as u64) << 8 |
                                                   (packet[4] & 0b10000000) as u64,
            reserved: packet[4] & 0b01111110,
            original_program_clock_reference_extension: ((packet[4] & 0b00000001) as u16) << 8 |
                                                        packet[5] as u16,
        }
    }

    fn size() -> usize {
        6
    }
}

#[derive(Debug)]
pub struct AdaptationFieldExtension<'a> {
    pub adaptation_field_extension_length: u8,
    pub reserved: u8,
    pub ltw: Option<LegalTimeWindow>,
    pub piecewise_rate: Option<u32>,
    pub seamless_splice: Option<SeamlessSplice>,
    pub trailing_reserved: &'a [u8],
}

impl<'a> AdaptationFieldExtension<'a> {
    fn new(packet: &'a [u8]) -> Self {
        let adaptation_field_extension_length = packet[0];
        let ltw_flag = (packet[1] & 0b10000000) != 0;
        let piecewise_rate_flag = (packet[1] & 0b01000000) != 0;
        let seamless_splice_flag = (packet[1] & 0b00100000) != 0;
        let reserved = packet[1] & 0b00011111;

        let mut index = 2;

        let ltw = if ltw_flag {
            let ltw = LegalTimeWindow::new(&packet[index..]);
            index += LegalTimeWindow::size();
            Some(ltw)
        } else {
            None
        };

        let piecewise_rate = if piecewise_rate_flag {
            let rate = ((packet[index] & 0b00111111) as u32) << 16 |
                       ((packet[index + 1] as u32) << 16) |
                       (packet[index + 1] as u32);
            index += 3;
            Some(rate)
        } else {
            None
        };

        let seamless_splice = if seamless_splice_flag {
            let splice = SeamlessSplice::new(&packet[index..]);
            index += SeamlessSplice::size();
            Some(splice)
        } else {
            None
        };

        let trailing_reserved = &packet[index..];

        AdaptationFieldExtension {
            adaptation_field_extension_length: adaptation_field_extension_length,
            reserved: reserved,
            ltw: ltw,
            piecewise_rate: piecewise_rate,
            seamless_splice: seamless_splice,
            trailing_reserved: trailing_reserved,
        }
    }
}

#[derive(Debug)]
pub struct LegalTimeWindow {
    pub ltw_valid_flag: bool,
    pub ltw_offset: u16,
}

impl LegalTimeWindow {
    fn new(packet: &[u8]) -> Self {
        LegalTimeWindow {
            ltw_valid_flag: (packet[0] & 0b10000000) != 0,
            ltw_offset: ((packet[0] & 0b01111111) as u16) << 8 | (packet[1] as u16),
        }
    }

    fn size() -> usize {
        2
    }
}

#[derive(Debug)]
pub struct SeamlessSplice {
    pub splice_type: u8,
    pub dts_next_au: u64,
}

impl SeamlessSplice {
    fn new(packet: &[u8]) -> Self {
        SeamlessSplice {
            splice_type: packet[0] & 0b11110000,
            dts_next_au: ((((packet[0] & 0b00001110) >> 1) as u64) << 30 |
                          ((packet[1] >> 1) as u64) << 15 |
                          ((packet[2] >> 1) as u64)),
        }
    }

    fn size() -> usize {
        5
    }
}
