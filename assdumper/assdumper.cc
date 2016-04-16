#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <vector>
#include <dvbpsi/descriptor.h>
#include <dvbpsi/dr_52.h>
#include <dvbpsi/dvbpsi.h>
#include <dvbpsi/pat.h>
#include <dvbpsi/pmt.h>
#include <dvbpsi/psi.h>
#include <dvbpsi/tot.h>
#include <dvbpsi/demux.h>
extern "C" {
// https://github.com/nkoriyama/aribb24/pull/7
#include <aribb24/aribb24.h>
#include <aribb24/parser.h>
#include <aribb24/decoder.h>
};

static void on_message(dvbpsi_t *, const dvbpsi_msg_level_t level,
                       const char *msg) {
  std::cerr << "[" << level << "] " << msg << std::endl;
}

static const uint16_t INVALID_PID = -1;
static const size_t TS_PACKET_SIZE = 188;

class pmt_parser {
  dvbpsi_t *dvbpsi_;
  uint16_t caption_pid_;

  static void pmt_callback(void *data, dvbpsi_pmt_t *pmt) {
    pmt_parser *parser = static_cast<decltype(parser)>(data);
    parser->on_pmt(pmt);
    dvbpsi_pmt_delete(pmt);
  }

public:
  pmt_parser(uint16_t program_number)
      : dvbpsi_(dvbpsi_new(on_message, DVBPSI_MSG_WARN)),
        caption_pid_(INVALID_PID) {
    dvbpsi_pmt_attach(dvbpsi_, program_number, pmt_callback, this);
  }
  pmt_parser(const pmt_parser &) = delete;
  ~pmt_parser() {
    dvbpsi_pmt_detach(dvbpsi_);
    dvbpsi_delete(dvbpsi_);
  }

  void push_packet(uint8_t *buf) { dvbpsi_packet_push(dvbpsi_, buf); }

  void on_pmt(const dvbpsi_pmt_t *pmt) {
    for (const dvbpsi_pmt_es_t *es = pmt->p_first_es; es != nullptr;
         es = es->p_next) {
      if (es->i_type == 0x06) {
        for (dvbpsi_descriptor_t *desc = es->p_first_descriptor;
             desc != nullptr; desc = desc->p_next) {
          if (desc->i_tag == 0x52) {
            const dvbpsi_stream_identifier_dr_t *si =
                dvbpsi_DecodeStreamIdentifierDr(desc);
            if (si->i_component_tag == 0x87) {
              std::cout << "Found caption PID: pid=" << es->i_pid
                        << ", pcr_pid=" << pmt->i_pcr_pid << std::endl;
              caption_pid_ = es->i_pid;
            }
          }
        }
      }
    }
  }

  uint16_t caption_pid() const { return caption_pid_; }
};

class tot_parser {
  dvbpsi_t *dvbpsi_;
  uint8_t table_id_;
  time_t time_;

  static void new_subtable_callback(dvbpsi_t *dvbpsi, uint8_t table_id,
                                    uint16_t extension, void *data) {
    if (table_id == 0x70 || table_id == 0x73) {
      dvbpsi_tot_attach(dvbpsi, table_id, extension, tot_callback, data);
    }
  }

  static void tot_callback(void *data, dvbpsi_tot_t *tot) {
    static_cast<tot_parser *>(data)->on_tot(tot);
    dvbpsi_tot_delete(tot);
  }

  static time_t decode_utc(uint64_t utc_time) {
    uint16_t mjd = (utc_time >> 24);
    struct tm t;
    unsigned yy = (mjd - 15078.2) / 365.25;
    unsigned mm = (mjd - 14956.1 - unsigned(yy * 365.25)) / 30.6001;
    t.tm_mday = mjd - 14956 - unsigned(yy * 365.25) - int(mm * 30.6001);
    unsigned k = (mm == 14 || mm == 15) ? 1 : 0;
    t.tm_year = yy + k;
    t.tm_mon = mm - 1 - k * 12;
    t.tm_hour = decode_bcd((utc_time >> 16) & 0xff);
    t.tm_min = decode_bcd((utc_time >> 8) & 0xff);
    t.tm_sec = decode_bcd((utc_time >> 0) & 0xff);
    return mktime(&t);
  }

  static unsigned decode_bcd(uint8_t n) { return (n >> 4) * 10 + (n & 0x0f); }

public:
  tot_parser(uint8_t table_id)
      : dvbpsi_(dvbpsi_new(on_message, DVBPSI_MSG_WARN)), table_id_(table_id) {
    dvbpsi_AttachDemux(dvbpsi_, new_subtable_callback, this);
  }
  tot_parser(const tot_parser &) = delete;
  ~tot_parser() {
    dvbpsi_DetachDemux(dvbpsi_);
    dvbpsi_delete(dvbpsi_);
  }

  void push_packet(uint8_t *buf) { dvbpsi_packet_push(dvbpsi_, buf); }

  void on_tot(const dvbpsi_tot_t *tot) { time_ = decode_utc(tot->i_utc_time); }

  time_t time() const { return time_; }
};

class caption_parser {
  uint16_t pid_;
  uint8_t continuity_counter_;
  std::vector<uint8_t> payload_;
  arib_instance_t *arib_;

public:
  caption_parser(uint16_t pid)
      : pid_(pid), continuity_counter_(0xff), arib_(arib_instance_new(this)) {}
  caption_parser(const caption_parser &) = delete;
  ~caption_parser() { arib_instance_destroy(arib_); }

  std::string parse_packet(uint8_t *packet) {
    const bool payload_unit_start_indicator = (packet[1] & 0x40) != 0;
    const uint8_t continuity_counter = packet[3] & 0x0f;
    const bool has_adaptation = (packet[3] & 0x20) != 0;

    unsigned skip = 0;
    if (has_adaptation) {
      const unsigned adaptation_field_length = packet[4];
      skip = 5 + adaptation_field_length;
    } else {
      skip = 4;
    }

    if (continuity_counter_ == 0xff) {
      // first packet
      continuity_counter_ = continuity_counter;
    } else {
      const uint8_t next_cc = (continuity_counter_ + 1) & 0x0f;
      if (next_cc != continuity_counter) {
        std::cerr << "[WARN] unexpected continuity_counter: received "
                  << int(continuity_counter) << " instead of " << int(next_cc)
                  << std::endl;
      }
      continuity_counter_ = next_cc;
    }

    std::string caption;
    if (skip >= TS_PACKET_SIZE) {
      return caption;
    }

    if (payload_unit_start_indicator) {
      if (!payload_.empty()) {
        caption = parse_pes(payload_.data(), payload_.size());
      }
      payload_.clear();
    }
    for (unsigned i = skip; i < TS_PACKET_SIZE; i++) {
      payload_.push_back(packet[i]);
    }
    return caption;
  }

  std::string parse_pes(uint8_t *pes_packet, size_t size) {
    const uint32_t packet_start_code_prefix =
        (pes_packet[0] << 16) | (pes_packet[1] << 8) | pes_packet[2];
    if (packet_start_code_prefix != 1) {
      return "";
    }
    const uint8_t prefix = pes_packet[6] >> 6;
    if (prefix != 0b10) {
      std::cerr << "Invalid PES packet: prefix=" << int(prefix) << std::endl;
      return "";
    }
    const uint8_t PES_header_data_length = pes_packet[8];

    const unsigned skip = 9 + PES_header_data_length;
    return parse_pes_data(pes_packet + skip, size - skip);
  }

  std::string parse_pes_data(uint8_t *pes_data, size_t size) {
    arib_parser_t *parser = arib_get_parser(arib_);
    arib_decoder_t *decoder = arib_get_decoder(arib_);

    arib_parse_pes(parser, pes_data, size);
    size_t data_size = 0;
    const uint8_t *data = arib_parser_get_data(parser, &data_size);
    if (data_size == 0) {
      return "";
    }
    const size_t buf_size = data_size * 4;
    char *buf = new char[buf_size];
    arib_initialize_decoder_c_profile(decoder);
    const size_t caption_size =
        arib_decode_buffer(decoder, data, data_size, buf, buf_size);
    if (caption_size == 0) {
      return "";
    }
    const std::string caption(buf, buf + caption_size);
    delete[] buf;
    return caption;
  }
};

class ass_dumper {
  dvbpsi_t *dvbpsi_;
  std::map<uint16_t, std::unique_ptr<pmt_parser>> pmt_parsers_;
  std::map<uint16_t, std::unique_ptr<caption_parser>> caption_parsers_;
  const tot_parser &tot_parser_;
  std::string previous_caption_;
  struct tm previous_tm_;
  bool prelude_printed_;

  static void pat_callback(void *data, dvbpsi_pat_t *pat) {
    static_cast<ass_dumper *>(data)->on_pat(pat);
    dvbpsi_pat_delete(pat);
  }

  void dump_caption(uint16_t pid, uint8_t *buf) {
    auto it = caption_parsers_.find(pid);
    if (it == caption_parsers_.end()) {
      caption_parsers_.insert(std::make_pair(
          pid, std::unique_ptr<caption_parser>(new caption_parser(pid))));
    }
    const std::string caption =
        caption_parsers_.find(pid)->second->parse_packet(buf);
    dump_dialog(caption);
  }

  void dump_dialog(const std::string &caption) {
    const time_t unix_time = tot_parser_.time();
    struct tm t;
    localtime_r(&unix_time, &t);

    if (!previous_caption_.empty()) {
      print_prelude_if_needed();
      // TODO: Use PCR for more accurate timing.
      std::printf("Dialogue: "
                  "0,%02d:%02d:%02d.%02d,%02d:%02d:%02d.%02d,Default,,,,,,%s\n",
                  previous_tm_.tm_hour, previous_tm_.tm_min,
                  previous_tm_.tm_sec, 0, t.tm_hour, t.tm_min, t.tm_sec, 0,
                  previous_caption_.c_str());
    }

    previous_caption_ = caption;
    previous_tm_ = t;
  }

  void print_prelude_if_needed() {
    if (!prelude_printed_) {
      std::cout << "[Script Info]" << std::endl
                << "ScriptType: v4.00+" << std::endl
                << "Collisions: Normal" << std::endl
                << "ScaledBorderAndShadow: yes" << std::endl
                << "Timer: 100.0000" << std::endl
                << std::endl
                << "[Events]" << std::endl;
      prelude_printed_ = true;
    }
  }

public:
  ass_dumper(const tot_parser &tot_parser)
      : dvbpsi_(dvbpsi_new(on_message, DVBPSI_MSG_WARN)),
        tot_parser_(tot_parser), prelude_printed_(false) {
    dvbpsi_pat_attach(dvbpsi_, pat_callback, this);
  }
  ass_dumper(const ass_dumper &) = delete;
  ~ass_dumper() {
    dvbpsi_pat_detach(dvbpsi_);
    dvbpsi_delete(dvbpsi_);
  }

  void push_pat_packet(uint8_t *buf) { dvbpsi_packet_push(dvbpsi_, buf); }

  void push_packet(uint16_t pid, uint8_t *buf) {
    auto it = pmt_parsers_.find(pid);
    if (it != pmt_parsers_.end()) {
      it->second->push_packet(buf);
    } else if (pid == caption_pid()) {
      dump_caption(pid, buf);
    }
  }

  void on_pat(const dvbpsi_pat_t *pat) {
    for (dvbpsi_pat_program_t *program = pat->p_first_program;
         program != nullptr; program = program->p_next) {
      pmt_parsers_.insert(std::make_pair(
          program->i_pid,
          std::unique_ptr<pmt_parser>(new pmt_parser(program->i_number))));
    }
  }

  uint16_t caption_pid() const {
    for (const auto &p : pmt_parsers_) {
      uint16_t pid = p.second->caption_pid();
      if (pid != INVALID_PID) {
        return pid;
      }
    }
    return INVALID_PID;
  }
};

int main(int argc, char *argv[]) {
  if (argc == 1) {
    return 1;
  }

  tot_parser tot_parser(0x73);
  ass_dumper ass_dumper(tot_parser);

  std::ifstream ifs(argv[1]);
  if (!ifs) {
    return 1;
  }
  uint8_t buf[TS_PACKET_SIZE];
  while (!ifs.eof()) {
    ifs.read(reinterpret_cast<char *>(buf), sizeof(buf));
    if (buf[0] != 0x47) {
      std::cerr << "sync_byte failed" << std::endl;
      continue;
    }
    uint16_t pid = (uint16_t(buf[1] & 0x1f) << 8) | buf[2];
    switch (pid) {
    case 0x0000:
      ass_dumper.push_pat_packet(buf);
      break;
    case 0x0014:
      tot_parser.push_packet(buf);
      break;
    case 0x1fff:
      break;
    default:
      ass_dumper.push_packet(pid, buf);
      break;
    }
  }

  return 0;
}
