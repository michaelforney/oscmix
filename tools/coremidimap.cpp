/* 
   
   coremidimap - A tool for iOS/macOS, to mirror CoreMIDI traffic from any source to any destination without blocking ports (for other Apps). 

   --------------------------------------------------------------------
   * The code is based on old examples from Apple Developer Resources *
   --------------------------------------------------------------------

 - Requirements:
   On iPad, a Console/Terminal-App is needed, as we want to execute a binary.
   
   -------------------
   
 - Building (on Mac) for iPad (iOS 9.3.x) 
   clang coremidimap.cpp -arch armv7 -isysroot /path/to/iPhoneOS9.3.sdk -miphoneos-version-min=9.0 -framework CoreMIDI -framework CoreFoundation -o coremidimap_iOS_armv7
 
 - Building (on Mac) for macOS (M1 and Intel):
   clang coremidimap.cpp -arch arm64 -arch x86_64 -framework CoreMIDI -framework CoreFoundation -o coremidimap_macOS_universal
 
   --------------------
 
   To be abe to run on iOS/macOS, you maybe need to codesignin the binaries, first.
   Example, via ad-hoc signing:
   codesign --sign - --force /path/to/coremidimap_iOS_armv7
   and/or...
   codesign --sign - --force /path/to/coremidimap_macOS_universal
   
   ---------------------
   
 - Example Output (on an iPad Terminal):
   
    ./coremidimap_iOS_armv7
	Available MIDI Sources:
	1: Session 1
	2: Port 1
	3: Port 2
	Select a MIDI Source (1-3): 3
	Selected MIDI Source: 3
	
	Available MIDI Destinations:
	1: Session 1
	2: Port 1
	3: Port 2
	Select a MIDI Destination (1-3): 1
	Selected MIDI Destination: 1
 
 
 - You can also create bidirectional routings. Open a second Terminal:
 
    ./coremidimap_iOS_armv7
	Available MIDI Sources:
	1: Session 1
	2: Port 1
	3: Port 2
	Select a MIDI Source (1-3): 1
	Selected MIDI Source: 1
	
	Available MIDI Destinations:
	1: Session 1
	2: Port 1
	3: Port 2
	Select a MIDI Destination (1-3): 3
	Selected MIDI Destination: 3
*/

#include <CoreMIDI/MIDIServices.h>
#include <CoreFoundation/CFRunLoop.h>
#include <stdio.h>

MIDIPortRef     gOutPort = NULL;
MIDIEndpointRef gSrc = NULL;
MIDIEndpointRef gDest = NULL;
int             gChannel = 0;

static void MyReadProc(const MIDIPacketList *pktlist, void *refCon, void *connRefCon)
{
    if (gOutPort != NULL && gSrc != NULL && gDest != NULL) {
        MIDIPacket *packet = (MIDIPacket *)pktlist->packet;
        for (unsigned int j = 0; j < pktlist->numPackets; ++j) {
            for (int i = 0; i < packet->length; ++i) {
             // printf("%02X ", packet->data[i]); // enable this if you want to see the traffic in terminal
                if (packet->data[i] >= 0x80 && packet->data[i] < 0xF0)
                    packet->data[i] = (packet->data[i] & 0xF0) | gChannel;
            }
            // printf("\n");  // enable this if you want to see the traffic in terminal
            packet = MIDIPacketNext(packet);
        }
        MIDISend(gOutPort, gDest, pktlist);
    }
}

static void selectSource() {
    int n = MIDIGetNumberOfSources();
    printf("Available MIDI Sources:\n");
    for (int i = 0; i < n; ++i) {
        MIDIEndpointRef src = MIDIGetSource(i);
        CFStringRef pname;
        MIDIObjectGetStringProperty(src, kMIDIPropertyName, &pname);
        char name[256];
        CFStringGetCString(pname, name, sizeof(name), 0);
        CFRelease(pname);
        printf("%d: %s\n", i+1, name);
    }

    printf("Select a MIDI Source (1-%d): ", n);
    int selection;
    scanf("%d", &selection);
    if (selection > 0 && selection <= n) {
        gSrc = MIDIGetSource(selection - 1);
        printf("Selected MIDI Source: %d\n", selection);
    } else {
        printf("Invalid selection.\n");
        selectSource();
    }
}

static void selectDestination() {
    int n = MIDIGetNumberOfDestinations();
    printf("Available MIDI Destinations:\n");
    for (int i = 0; i < n; ++i) {
        MIDIEndpointRef dest = MIDIGetDestination(i);
        CFStringRef pname;
        MIDIObjectGetStringProperty(dest, kMIDIPropertyName, &pname);
        char name[256];
        CFStringGetCString(pname, name, sizeof(name), 0);
        CFRelease(pname);
        printf("%d: %s\n", i+1, name);
    }

    printf("Select a MIDI Destination (1-%d): ", n);
    int selection;
    scanf("%d", &selection);
    if (selection > 0 && selection <= n) {
        gDest = MIDIGetDestination(selection - 1);
        printf("Selected MIDI Destination: %d\n", selection);
    } else {
        printf("Invalid selection.\n");
        selectDestination();
    }
}

int main(int argc, char *argv[])
{
    selectSource();
    selectDestination();

    if (argc >= 3) {
        sscanf(argv[2], "%d", &gChannel);
        if (gChannel < 1) gChannel = 1;
        else if (gChannel > 16) gChannel = 16;
        --gChannel;
    }

    MIDIClientRef client = NULL;
    MIDIClientCreate(CFSTR("MIDI Echo"), NULL, NULL, &client);

    MIDIPortRef inPort = NULL;
    MIDIInputPortCreate(client, CFSTR("Input port"), MyReadProc, NULL, &inPort);
    MIDIPortConnectSource(inPort, gSrc, NULL);

    MIDIOutputPortCreate(client, CFSTR("Output port"), &gOutPort);

    CFRunLoopRun();

    return 0;
}
