// regtool_osx.cpp
// compile it via:
// g++ -std=c++11  -D__MACOSX_CORE__ -o regtool_osx regtool_osx.cpp RtMidi.cpp -framework CoreMIDI -framework CoreAudio -framework CoreFoundation

#include <iostream>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "RtMidi.h"
#include "../sysex.h"
#include "../arg.h"

static RtMidiIn *midiin = nullptr;
static RtMidiOut *midiout = nullptr;


int sflag = 0;
int wflag = 0;

void usage() {
	std::cerr << "Usage:\n"
			  << "	regtool_osx -l	List available MIDI I/O port numbers and names\n"
			  << "	regtool_osx [-s] <MIDI IN port number or name>\n"
			  << "	regtool_osx [-s] -w <MIDI OUT port number or name> [reg val]...\n";
			  
	exit(1);
}

static void
dumpsysex(const char *prefix, const unsigned char *buf, size_t len)
{
	static const unsigned char hdr[] = {0xf0, 0x00, 0x20, 0x0d, 0x10};
	const unsigned char *pos, *end;
	unsigned long regval;
	unsigned reg, val, par;

	pos = buf;
	end = pos + len;
	if (sflag) {
		fputs(prefix, stdout);
		for (; pos != end; ++pos)
			printf(" %.2X", *pos);
		fputc('\n', stdout);
	}
	pos = buf;
	--end;
	if (len < sizeof hdr || memcmp(pos, hdr, sizeof hdr) != 0 || (len - sizeof hdr - 2) % 5 != 0) {
		printf("skipping unexpected sysex\n");
		return;
	}
	if (pos[5] != 0) {
		printf("subid=%d", pos[5]);
		for (pos += sizeof hdr + 1; pos != end; pos += 5) {
			regval = getle32_7bit(pos);
			printf("%c%.8lX", pos == buf + sizeof hdr + 1 ? '\t' : ' ', regval);
		}
		fputc('\n', stdout);
		return;
	}
	for (pos += sizeof hdr + 1; pos != end; pos += 5) {
		regval = getle32_7bit(pos);
		reg = regval >> 16 & 0x7fff;
		val = regval & 0xffff;
		par = regval ^ regval >> 16 ^ 1;
		par ^= par >> 8;
		par ^= par >> 4;
		par ^= par >> 2;
		par ^= par >> 1;
		printf("%.4X\t%.4X", reg, val);
		if (par & 1)
			printf("bad parity");
		fputc('\n', stdout);
	}
	fflush(stdout);
}

static void 
midiread(void) {
	std::vector<unsigned char> message;
	size_t len = 0;
	static unsigned char buf[8192];

	while (true) {
		message.clear();
		midiin->getMessage(&message);
		size_t nBytes = message.size();

		if (nBytes == 0) continue;
		
		const unsigned char* evtbuf = message.data();
		size_t evtlen = nBytes;

		if (evtbuf[0] == 0xF0) {
			if (len > 0) {
				std::cerr << "dropping incomplete sysex\n";
				len = 0;
			}
		}

		if (evtlen > sizeof(buf) - len) {
			std::cerr << "dropping sysex that is too long\n";
			len = (evtbuf[evtlen - 1] == 0xF7) ? 0 : sizeof(buf); 
			continue;
		}

		memcpy(buf + len, evtbuf, evtlen);
		len += evtlen;

		if (buf[len - 1] == 0xF7) {
			dumpsysex("<-", buf, len);
			len = 0; 
		}
	}
}

static void 
setreg(unsigned reg, unsigned val) {
	std::vector<unsigned char> buf = {0xf0, 0x00, 0x20, 0x0d, 0x10, 0x00};
	reg &= 0x7fff;
	val &= 0xffff;
	unsigned par = reg ^ val ^ 1;
	par ^= par >> 8;
	par ^= par >> 4;
	par ^= par >> 2;
	par ^= par >> 1;
	unsigned long regval = par << 31 | reg << 16 | val;
	// encode regval into the 7-Bit-Format and add it to message vektor
	for (int i = 0; i < 5; ++i) {
		buf.push_back(((regval >> (7 * i)) & 0x7F));
	}
	buf.push_back(0xf7); // Add SysEx Ende-Byte
	// use dumpsysex, to log our message before sending
	dumpsysex("->", buf.data(), buf.size());
	// Send the message
	try {
		midiout->sendMessage(&buf);
	} catch (RtMidiError &error) {
		std::cerr << "Failed to send MIDI message: " << error.getMessage() << std::endl;
	}
}

static void
midiwrite(void){
	unsigned reg, val;
	char str[256];
	while (fgets(str, sizeof str, stdin)) {
		if (sscanf(str, "%x %x", &reg, &val) != 2) {
			fprintf(stderr, "invalid input\n");
			continue;
		}
		setreg(reg, val);
	}
}

int main(int argc, char* argv[]) {
	if (argc < 2) usage();
	// Parsing command line arguments
	int portArgIndex = 1; // Index of Argument, contains Port-Identifier
	for (int i = 1; i < argc; ++i) {
		std::string arg = argv[i];
		if (arg == "-l") {
			RtMidiIn* tempMidiIn = new RtMidiIn();
			RtMidiOut* tempMidiOut = new RtMidiOut();
		// Check inputs.
		unsigned int nPorts = tempMidiIn->getPortCount();
		std::cout << "\n" << nPorts << " MIDI input source(s) available:\n";
		std::string portName;
		for ( unsigned int i=0; i<nPorts; i++ ) {
			try {
				portName = tempMidiIn->getPortName(i);
			}
			catch ( RtMidiError &error ) {
			error.printMessage();
			goto cleanup;
			}
		std::cout << "	" << i << ": " << portName << '\n';
		}
		// RtMidiOut constructor
		try {
			tempMidiOut = new RtMidiOut();
		}
		catch ( RtMidiError &error ) {
			error.printMessage();
			exit( EXIT_FAILURE );
		}
		// Check outputs.
		nPorts = tempMidiOut->getPortCount();
		std::cout << "\n" << nPorts << " MIDI output destination(s) available:\n";
		for ( unsigned int i=0; i<nPorts; i++ ) {
			try {
				portName = tempMidiOut->getPortName(i);
			}
			catch (RtMidiError &error) {
				error.printMessage();
				goto cleanup;
			}
		std::cout << "	" << i << ": " << portName << '\n';
		}
		std::cout << '\n' << "Re-run this tool again with the port number or port name of your device.\n\n";
		cleanup:
			delete tempMidiIn;
			delete tempMidiOut;
			usage();
		} else if (arg == "-s") {
			sflag = 1;
			portArgIndex = i + 1;
		} else if (arg == "-w") {
			wflag = 1;
			portArgIndex = i + 1;
		} else if (i <= portArgIndex) {
			// First Argument after Flags is the Port-Identifier
			break;
		} else {
			// if we receive a further Argument
			break;
		}
	}
	if (portArgIndex >= argc) {
		std::cerr << "No MIDI port specified.\n";
		usage();
	}
	std::string portIdentifier = argv[portArgIndex];
	bool isNumber = std::all_of(portIdentifier.begin(), portIdentifier.end(), ::isdigit);
	try {
		unsigned int portNumber = 0;
		bool portOpened = false;
		if (wflag) {
			midiout = new RtMidiOut();
			unsigned int nPorts = midiout->getPortCount();
			if (isNumber) {
				portNumber = std::stoi(portIdentifier);
				if (portNumber < nPorts) {
					midiout->openPort(portNumber);
					portOpened = true;
				}
			} else {
				for (unsigned int i = 0; i < nPorts; i++) {
					if (midiout->getPortName(i) == portIdentifier) {
						midiout->openPort(i);
						portOpened = true;
						break;
					}
				}
			}
		} else {
			midiin = new RtMidiIn();
			midiin->ignoreTypes(false, false, false);
			unsigned int nPorts = midiin->getPortCount();
			if (isNumber) {
				portNumber = std::stoi(portIdentifier);
				if (portNumber < nPorts) {
					midiin->openPort(portNumber);
					portOpened = true;
				}
			} else {
				for (unsigned int i = 0; i < nPorts; i++) {
					if (midiin->getPortName(i) == portIdentifier) {
						midiin->openPort(i);
					   // midiin->ignoreTypes(false, false, false);
						portOpened = true;
						break;
					}
				}
			}
		}
		if (!portOpened) {
			std::cerr << "Could not find or open the specified MIDI port." << std::endl;
			return 1;
		}
	}
	catch (RtMidiError &error) {
		error.printMessage();
		return 1;
	}
	// Handling MIDI messages based on wflag
	if (wflag && argc > portArgIndex + 1) {
		for (int i = portArgIndex + 1; i < argc; i += 2) {
			char* end;
			long reg = std::strtol(argv[i], &end, 16);
			if (*end || reg < 0 || reg > 0x7fff) usage();
			long val = std::strtol(argv[i + 1], &end, 16);
			if (*end || val < -0x8000 || val > 0xffff) usage();
			setreg(static_cast<unsigned>(reg), static_cast<unsigned>(val));
		}
	} else if (!wflag) {
		midiread();
	} 
	else {
		midiwrite();
	}
	delete midiin;
	delete midiout;
	return 0;
}