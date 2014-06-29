package main

import "fmt"
import "io"
import "os"

/*
ISO: ISO/IEC 13818-1
*/

const TS_PACKET_SIZE = 188

type AnalyzerState struct {
	pmtPids map[int]bool
}

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

	//payload_unit_start_indicator := (packet[1] & 0x40) != 0
	pid := int(packet[1]&0x1f)<<8 | int(packet[2])
	hasAdaptation := (packet[3] & 0x20) != 0
	hasPayload := (packet[3] & 0x10) != 0
	p := packet[4:]

	if hasAdaptation {
		adaptation_field_length := p[0]
		p = p[1:]
		p = p[adaptation_field_length:]
	}

	if hasPayload {
		if pid == 0 {
			if len(state.pmtPids) == 0 {
				state.pmtPids = extractPmtPids(p[1:])
				fmt.Fprintf(os.Stderr, "Found %d pids: %v\n", len(state.pmtPids), state.pmtPids)
			}
		}
	}
}

func extractPmtPids(payload []byte) map[int]bool {
	// ISO 2.4.4.3
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
			fmt.Printf("program_number = %d, program_map_PID = %d\n", program_number, program_map_PID)
			pids[program_map_PID] = true
		}
		index += 4
	}
	return pids
}
