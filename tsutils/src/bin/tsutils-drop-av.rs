extern crate env_logger;
extern crate tsutils;

#[macro_use]
extern crate log;

fn main() {
    env_logger::init().unwrap();

    let input = std::io::stdin();
    let output = std::io::stdout();
    drop_av(input, output).unwrap();
}

#[derive(Debug)]
pub enum Error {
    Io(std::io::Error),
    PsiParseError(tsutils::psi::ParseError),
    Custom(std::borrow::Cow<'static, str>),
}

impl From<std::io::Error> for Error {
    fn from(e: std::io::Error) -> Self {
        Error::Io(e)
    }
}

impl From<&'static str> for Error {
    fn from(e: &'static str) -> Self {
        Error::Custom(std::borrow::Cow::from(e))
    }
}

impl From<String> for Error {
    fn from(e: String) -> Self {
        Error::Custom(std::borrow::Cow::from(e))
    }
}

impl From<tsutils::psi::ParseError> for Error {
    fn from(e: tsutils::psi::ParseError) -> Self {
        Error::PsiParseError(e)
    }
}

fn drop_av<R, W>(reader: R, mut writer: W) -> Result<(), Error>
    where R: std::io::Read,
          W: std::io::Write
{
    let mut pat = None;
    let mut payloads: std::collections::HashMap<u16, Vec<u8>> = std::collections::HashMap::new();
    let mut av_pids = std::collections::HashSet::new();
    let mut nonav_pids = std::collections::HashSet::new();

    for buf in tsutils::packet::ts_packets(reader) {
        let buf = try!(buf);
        let packet = tsutils::TsPacket::new(&buf);
        if !packet.check_sync_byte() {
            return Err(Error::from("sync_byte failed"));
        }
        if packet.transport_error_indicator {
            return Err(Error::from("transport_error_indicator is set"));
        }

        if packet.payload_unit_start_indicator {
            if let Some(payload) = payloads.get(&packet.pid) {
                match packet.pid {
                    0x0000 => {
                        pat = Some(try!(tsutils::ProgramAssociationTable::parse(payload)));
                    }
                    _ => {
                        if let Some(ref pat) = pat {
                            if let Some(&program_number) = pat.program_map.get(&packet.pid) {
                                let pmt = try!(tsutils::ProgramMapTable::parse(payload));
                                if pmt.program_number != program_number {
                                    return Err(Error::from(format!("Inconsistent \
                                                                    program_number for PID={}: \
                                                                    PAT says {} but PMT says {}",
                                                                   packet.pid,
                                                                   program_number,
                                                                   pmt.program_number)));
                                }
                                for es in pmt.es_info {
                                    if !av_pids.contains(&es.elementary_pid) &&
                                       !nonav_pids.contains(&es.elementary_pid) {
                                        match es.stream_type {
                                            0x0f => {
                                                // Audio
                                                av_pids.insert(es.elementary_pid);
                                            }
                                            0x02 | 0x1b => {
                                                // Video
                                                av_pids.insert(es.elementary_pid);
                                            }
                                            _ => {
                                                debug!("non-AV stream_type={:x} pid={:x}",
                                                       es.stream_type,
                                                       es.elementary_pid);
                                                nonav_pids.insert(es.elementary_pid);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if let Some(data_bytes) = packet.data_bytes {
                payloads.insert(packet.pid, data_bytes);
            } else {
                return Err(Error::from("payload_unit_start_indicator is set, but no \
                                                 data_bytes"));
            }
        } else {
            if let Some(mut data_bytes) = packet.data_bytes {
                match payloads.entry(packet.pid) {
                    std::collections::hash_map::Entry::Occupied(mut entry) => {
                        entry.get_mut().append(&mut data_bytes);
                    }
                    std::collections::hash_map::Entry::Vacant(entry) => {
                        entry.insert(data_bytes);
                    }
                }
            }
        }

        if !av_pids.contains(&packet.pid) {
            try!(writer.write(&buf));
        }
    }
    Ok(())
}
