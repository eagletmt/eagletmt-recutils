package main

import "fmt"
import "io"
import "os"

/*
[B10]: ARIB-STD B10
[ISO]: ISO/IEC 13818-1
*/

const TS_PACKET_SIZE = 188

type AnalyzerState struct {
	pmtPids          map[int]bool
	pcrPid           int
	captionPid       int
	currentTimestamp SystemClock
}

type SystemClock int64

func main() {
	if len(os.Args) == 1 {
		fmt.Fprintf(os.Stderr, "usage: %s MPEG2-TS-FILE\n", os.Args[0])
		os.Exit(1)
	}
	fin, err := os.Open(os.Args[1])
	if err != nil {
		panic(err)
	}
	defer func() {
		if err := fin.Close(); err != nil {
			panic(err)
		}
	}()

	buf := make([]byte, TS_PACKET_SIZE)
	state := new(AnalyzerState)
	state.pcrPid = -1
	state.captionPid = -1

	for {
		n, err := fin.Read(buf)
		if err != nil && err != io.EOF {
			panic(err)
		}
		if n == 0 {
			break
		}

		analyzePacket(buf, state)
	}
}

func assertSyncByte(packet []byte) {
	if packet[0] != 0x47 {
		panic("sync_byte failed")
	}
}

func analyzePacket(packet []byte, state *AnalyzerState) {
	assertSyncByte(packet)

	payload_unit_start_indicator := (packet[1] & 0x40) != 0
	pid := int(packet[1]&0x1f)<<8 | int(packet[2])
	hasAdaptation := (packet[3] & 0x20) != 0
	hasPayload := (packet[3] & 0x10) != 0
	p := packet[4:]

	if hasAdaptation {
		// [ISO] 2.4.3.4
		// Table 2-6
		adaptation_field_length := p[0]
		p = p[1:]
		pcr_flag := (p[0] & 0x10) != 0
		if pcr_flag && pid == state.pcrPid {
			state.currentTimestamp = extractPcr(p)
		}
		p = p[adaptation_field_length:]
	}

	if hasPayload {
		if pid == 0 {
			if len(state.pmtPids) == 0 {
				state.pmtPids = extractPmtPids(p[1:])
				fmt.Fprintf(os.Stderr, "Found %d pids: %v\n", len(state.pmtPids), state.pmtPids)
			}
		} else if state.pmtPids != nil && state.pmtPids[pid] {
			if state.captionPid == -1 && payload_unit_start_indicator {
				// PMT section
				pcrPid := extractPcrPid(p[1:])
				captionPid := extractCaptionPid(p[1:])
				if captionPid != -1 {
					fmt.Fprintf(os.Stderr, "caption pid = %d, PCR_PID = %d\n", captionPid, pcrPid)
					state.pcrPid = pcrPid
					state.captionPid = captionPid
				}
			}
		}
	}
}

func extractPmtPids(payload []byte) map[int]bool {
	// [ISO] 2.4.4.3
	// Table 2-25
	table_id := payload[0]
	pids := make(map[int]bool)
	if table_id != 0x00 {
		return pids
	}
	section_length := int(payload[1]&0x0F)<<8 | int(payload[2])
	index := 8
	for index < 3+section_length-4 {
		program_number := int(payload[index+0])<<8 | int(payload[index+1])
		if program_number != 0 {
			program_map_PID := int(payload[index+2]&0x1F)<<8 | int(payload[index+3])
			pids[program_map_PID] = true
		}
		index += 4
	}
	return pids
}

func extractPcrPid(payload []byte) int {
	return (int(payload[8]&0x1f) << 8) | int(payload[9])
}

func extractCaptionPid(payload []byte) int {
	// [ISO] 2.4.4.8 Program Map Table
	// Table 2-28
	table_id := payload[0]
	if table_id != 0x02 {
		return -1
	}
	section_length := int(payload[1]&0x0F)<<8 | int(payload[2])
	if section_length >= len(payload) {
		return -1
	}

	program_info_length := int(payload[10]&0x0F)<<8 | int(payload[11])
	index := 12 + program_info_length

	for index < 3+section_length-4 {
		stream_type := payload[index+0]
		ES_info_length := int(payload[index+3]&0xF)<<8 | int(payload[index+4])
		if stream_type == 0x06 {
			elementary_PID := int(payload[index+1]&0x1F)<<8 | int(payload[index+2])
			subIndex := index + 5
			for subIndex < index+ES_info_length {
				// [ISO] 2.6 Program and program element descriptors
				descriptor_tag := payload[subIndex+0]
				descriptor_length := int(payload[subIndex+1])
				if descriptor_tag == 0x52 {
					// [B10] 6.2.16 Stream identifier descriptor
					// 表 6-28
					component_tag := payload[subIndex+2]
					if component_tag == 0x87 {
						return elementary_PID
					}
				}
				subIndex += 2 + descriptor_length
			}
		}
		index += 5 + ES_info_length
	}
	return -1
}

func extractPcr(payload []byte) SystemClock {
	pcr_base := (int64(payload[1]) << 25) |
		(int64(payload[2]) << 17) |
		(int64(payload[3]) << 9) |
		(int64(payload[4]) << 1) |
		(int64(payload[5]&0x80) >> 7)
	pcr_ext := (int64(payload[5] & 0x01)) | int64(payload[6])
	// [ISO] 2.4.2.2
	return SystemClock(pcr_base*300 + pcr_ext)
}
