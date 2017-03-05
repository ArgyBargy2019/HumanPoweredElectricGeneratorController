//This is the name of the file on the microSD card you would like to play
//Stick with normal 8.3 nomeclature. All lower-case works well.
//Note: you must name the tracks on the SD card with 001, 002, 003, etc.
//For example, the code is expecting to play 'track002.mp3', not track2.mp3.
char trackName[] = "track001.mp3";
int trackNumber = 1;

byte pedalLevel = 0; // Stopped
byte pedalLevelPrev = 0;

void setup() {

    // Setup input pins here

    setupMP3();
    
    Serial.begin(57600); //Use serial for debugging
    Serial.println("MP3 Player Example using Control");
    
    trackNumber = 1; //Setup to play track001 by default
}

// TO IMPLEMENT: Return the current level of the speed as an integer - either 0 (stopped) 1, 2, or 3 (fastest)
byte getPedalLevel() {
  return 0;
}

bool simulFnCheckInput() {

    pedalLevel = getPedalLevel();
    if (pedalLevel != pedalLevelPrev) {
        return true;
    }

    pedalLevelPrev = pedalLevel;
    delay(10);
}

void loop() {

    // Wait for pedaling
    while (pedalLevel == 0) {
      pedalLevel = getPedalLevel(); // Check what speed the bike is moving at
    }

  int trackNumber = pedalLevel;
    
    // Play track corresponding to the pedal level
    sprintf(trackName, "track%03d.mp3", trackNumber); // Splice the new file number into this file name
    playMP3(trackName, &simulFnCheckInput); //Go play trackXXX.mp3
}




