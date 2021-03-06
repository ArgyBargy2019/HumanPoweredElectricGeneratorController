// Functions for playing MP3

#include <SPI.h>
#include <SD.h>
//#include <SdFat.h>
//#include <SdFatUtil.h> 

// TODO: Test using SD.h only vs all three: SD, SDFat, SDFatUtil

//MP3 Player Shield pin mapping. See the schematic
#define MP3_XCS 6 //Control Chip Select Pin (for accessing SPI Control/Status registers)
#define MP3_XDCS 7 //Data Chip Select / BSYNC Pin
#define MP3_DREQ 2 //Data Request Pin: Player asks for more data
#define MP3_RESET 8 //Reset is active low
// Remember you have to edit the Sd2PinMap.h of the sdfatlib library to correct control the SD card.

//VS10xx SCI Registers
#define SCI_MODE 0x00
#define SCI_STATUS 0x01
#define SCI_BASS 0x02
#define SCI_CLOCKF 0x03
#define SCI_DECODE_TIME 0x04
#define SCI_AUDATA 0x05
#define SCI_WRAM 0x06
#define SCI_WRAMADDR 0x07
#define SCI_HDAT0 0x08
#define SCI_HDAT1 0x09
#define SCI_AIADDR 0x0A
#define SCI_VOL 0x0B
#define SCI_AICTRL0 0x0C
#define SCI_AICTRL1 0x0D
#define SCI_AICTRL2 0x0E
#define SCI_AICTRL3 0x0F

// Create the variables to be used by SdFat Library
Sd2Card card;
SdVolume volume;
SdFile root;
SdFile track;

char errorMsg[100]; // This is a generic array used for sprintf of error messages

// Sets up the shield
void setupMP3() {
    pinMode(MP3_DREQ, INPUT);
    pinMode(MP3_XCS, OUTPUT);
    pinMode(MP3_XDCS, OUTPUT);
    pinMode(MP3_RESET, OUTPUT);
    
    digitalWrite(MP3_XCS, HIGH); //Deselect Control
    digitalWrite(MP3_XDCS, HIGH); //Deselect Data
    digitalWrite(MP3_RESET, LOW); //Put VS1053 into hardware reset

    // Setup SD card interface
    pinMode(10, OUTPUT); // Pin 10 must be set as an output for the SD communication to work.
    if (!card.init(SPI_FULL_SPEED, 9))  Serial.println("Error: Card init"); //Initialize the SD card, shield uses pin 9 for SD CS
    if (!volume.init(&card)) Serial.println("Error: Volume ini"); //Initialize a volume on the SD card.
    if (!root.openRoot(&volume)) Serial.println("Error: Opening root"); //Open the root directory in the volume.
    
    // We have no need to setup SPI for VS1053 because this has already been done by the SDfatlib
    
    //From page 12 of datasheet, max SCI reads are CLKI/7. Input clock is 12.288MHz.
    //Internal clock multiplier is 1.0x after power up.
    //Therefore, max SPI speed is 1.75MHz. We will use 1MHz to be safe.
    SPI.setClockDivider(SPI_CLOCK_DIV16); //Set SPI bus speed to 1MHz (16MHz / 16 = 1MHz)
    SPI.transfer(0xFF); //Throw a dummy byte at the bus
    
    //Initialize VS1053 chip
    delay(10);
    digitalWrite(MP3_RESET, HIGH); //Bring up VS1053
    
    //Mp3SetVolume(20, 20); //Set initial volume (20 = -10dB) LOUD
    Mp3SetVolume(40, 40); //Set initial volume (20 = -10dB) Manageable
    //Mp3SetVolume(80, 80); //Set initial volume (20 = -10dB) More quiet
    
    //Now that we have the VS1053 up and running, increase the internal clock multiplier and up our SPI rate
    Mp3WriteRegister(SCI_CLOCKF, 0x60, 0x00); //Set multiplier to 3.0x
    
    //From page 12 of datasheet, max SCI reads are CLKI/7. Input clock is 12.288MHz.
    //Internal clock multiplier is now 3x.
    //Therefore, max SPI speed is 5MHz. 4MHz will be safe.
    SPI.setClockDivider(SPI_CLOCK_DIV4); //Set SPI bus speed to 4MHz (16MHz / 4 = 4MHz)
}

// PlayMP3 plays a given file name
// It pulls 32 byte chunks from the SD card and throws them at the VS1053
// We monitor the DREQ (data request pin). If it goes low then we determine if
// we need new data or not. If yes, pull new from SD card. Then throw the data
// at the VS1053 until it is full.

// Also runs function simultaneously in timeslices between SPI communications.
// Function should return whether to stop playback early.
void playMP3(char* fileName, bool (*simultaneousFn)(void)) {
    
    if (!track.open(&root, fileName, O_READ)) { //Open the file in read mode.
        sprintf(errorMsg, "Failed to open %s", fileName);
        Serial.println(errorMsg);
        return;
    }
    
    sprintf(errorMsg, "Playing track %s", fileName);
    Serial.println(errorMsg);
    
    uint8_t mp3DataBuffer[32]; //Buffer of 32 bytes. VS1053 can take 32 bytes at a go.
    int need_data = true;
    
    while (true) {
        while (!digitalRead(MP3_DREQ)) {
            // DREQ is low while the receive buffer is full
            // Do simultaneous function here, since the buffer of the MP3 is full and happy.
            const bool shouldStop = (*simultaneousFn)();
            if (shouldStop) {
                track.close();
                break;
            }
            
            //If the MP3 IC is happy, but we need to read new data from the SD, now is a great time to do so
            if (need_data == true) {

                //Try reading 32 new bytes of the song
                if (!track.read(mp3DataBuffer, sizeof(mp3DataBuffer))) {
                    // There is no data left to read!
                    break;
                }
                need_data = false;
            }
        }

        //This is here in case we haven't had any free time to load new data
        if (need_data == true){

            //Go out to SD card and try reading 32 new bytes of the song
            if (!track.read(mp3DataBuffer, sizeof(mp3DataBuffer))) {
                // There is no data left to read!
                break;
            }
            need_data = false;
        }
        
        // Once DREQ is released (high) we now feed 32 bytes of data to the VS1053 from our SD read buffer
        digitalWrite(MP3_XDCS, LOW); //Select Data
        for (int y = 0 ; y < sizeof(mp3DataBuffer) ; y++) {
            // Send SPI byte
            SPI.transfer(mp3DataBuffer[y]);
        }
        
        digitalWrite(MP3_XDCS, HIGH); //Deselect Data
        need_data = true; //We've just dumped 32 bytes into VS1053 so our SD read buffer is empty. Set flag so we go get more data
    }

    // Wait for DREQ to go high indicating transfer is complete
    while (!digitalRead(MP3_DREQ));

    // Deselect data
    digitalWrite(MP3_XDCS, HIGH);

    // Close out this track
    track.close();
    
    sprintf(errorMsg, "Track %s done!", fileName);
    Serial.println(errorMsg);
}

// Write to VS10xx register
// SCI: Data transfers are always 16bit. When a new SCI operation comes in
// DREQ goes low. We then have to wait for DREQ to go high again.
// XCS should be low for the full duration of operation.
void Mp3WriteRegister(unsigned char addressbyte, unsigned char highbyte, unsigned char lowbyte) {

    // Wait for DREQ to go high indicating IC is available
    while (!digitalRead(MP3_DREQ));

    // Select control
    digitalWrite(MP3_XCS, LOW);
    
    // SCI consists of instruction byte, address byte, and 16-bit data word.
    SPI.transfer(0x02); // Write instruction
    SPI.transfer(addressbyte);
    SPI.transfer(highbyte);
    SPI.transfer(lowbyte);

    // Wait for DREQ to go high indicating command is complete
    while (!digitalRead(MP3_DREQ));

    // Deselect control
    digitalWrite(MP3_XCS, HIGH);
}

// Read the 16-bit value of a VS10xx register
unsigned int Mp3ReadRegister (unsigned char addressbyte) {

    //Wait for DREQ to go high indicating IC is available
    while (!digitalRead(MP3_DREQ));
    digitalWrite(MP3_XCS, LOW); // Select control
    
    // SCI consists of instruction byte, address byte, and 16-bit data word.
    SPI.transfer(0x03); // Read instruction
    SPI.transfer(addressbyte);
    
    char response1 = SPI.transfer(0xFF); // Read the first byte
    while(!digitalRead(MP3_DREQ)) ; // Wait for DREQ to go high indicating command is complete
    char response2 = SPI.transfer(0xFF); // Read the second byte
    while(!digitalRead(MP3_DREQ)) ; // Wait for DREQ to go high indicating command is complete
    
    digitalWrite(MP3_XCS, HIGH); // Deselect Control
    
    int resultvalue = response1 << 8;
    resultvalue |= response2;
    return resultvalue;
}

// Set VS10xx Volume Register
void Mp3SetVolume(unsigned char leftchannel, unsigned char rightchannel) {
    Mp3WriteRegister(SCI_VOL, leftchannel, rightchannel);
}
