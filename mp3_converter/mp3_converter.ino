#include <SPI.h>
#include <SD.h>
#include <arduinoFFT.h>
#include <FS.h>

#include "AudioFileSourceSD.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutput.h"

#define SD_CS 10
#define SPI_CLK 12
#define SPI_MISO 13
#define SPI_MOSI 11

#define SAMPLES 256
#define SAMPLING_FREQ 44100

double vReal[SAMPLES];
double vImag[SAMPLES];
unsigned long lastFFT = 0;

ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, SAMPLES, SAMPLING_FREQ);

AudioGeneratorMP3 *mp3;
AudioFileSourceSD *file;

File outFile;

void processFFT();
void calculateBands(int bands[7]);
void writeBands(int bands[7]);

void processSongs();
void processFile();

class AudioOutputCapture : public AudioOutput {
public:

  bool begin() { return true; }

  bool ConsumeSample(int16_t sample[2]) {

    static int i = 0;

    vReal[i] = sample[0];
    vImag[i] = 0;

    i++;

    if(i >= SAMPLES){
      processFFT();
      i = 0;
    }

    return true;
  }
};

AudioOutputCapture *capture;

void setup(){

  Serial.begin(115200);
  Serial.println("start");

  SPI.begin(SPI_CLK, SPI_MISO, SPI_MOSI, SD_CS);

  if(!SD.begin(SD_CS)){
    Serial.println("SD kaart niet gevonden");
    while(true);
  }

  Serial.println("SD OK");
  // processSongs();

  String fileName = "/song1.mp3";
  String outName  = "/song1.txt";

  if(!SD.exists(fileName)){
    Serial.println("song1.mp3 niet gevonden");
    return;
  }

  Serial.println("Verwerken: " + fileName);

  capture = new AudioOutputCapture();

  file = new AudioFileSourceSD(fileName.c_str());
  mp3 = new AudioGeneratorMP3();

  mp3->begin(file, capture);

  outFile = SD.open(outName, FILE_WRITE);

  processFile();

  mp3->stop();

  outFile.close();

  Serial.println("Klaar: " + outName);
  fileName = "/song2.mp3";
  outName  = "/song2.txt";

  if(!SD.exists(fileName)){
    Serial.println("song1.mp3 niet gevonden");
    return;
  }

  Serial.println("Verwerken: " + fileName);

  capture = new AudioOutputCapture();

  file = new AudioFileSourceSD(fileName.c_str());
  mp3 = new AudioGeneratorMP3();

  mp3->begin(file, capture);

  outFile = SD.open(outName, FILE_WRITE);

  processFile();

  mp3->stop();

  outFile.close();

  Serial.println("Klaar: " + outName);
}

void loop(){}

void processSongs(){

  File root = SD.open("/");
  File entry = root.openNextFile();

  while(entry){

    String name = entry.name();

    if(!entry.isDirectory() &&
       (name.endsWith(".mp3") || name.endsWith(".MP3")) &&
       !name.startsWith("._")){

      Serial.println("Verwerken: " + name);

      String txtName = "/" + name;
      txtName.replace(".mp3",".txt");
      txtName.replace(".MP3",".txt");

      capture = new AudioOutputCapture();

      file = new AudioFileSourceSD(name.c_str());
      mp3 = new AudioGeneratorMP3();

      mp3->begin(file, capture);

      outFile = SD.open(txtName, FILE_WRITE);

      if(!outFile){
        Serial.println("TXT file openen mislukt!");
        return;
      }

      processFile();

      mp3->stop();

      outFile.close();

      Serial.println("Klaar: " + txtName);
      Serial.println("-----------------------------");

    }

    entry = root.openNextFile();
  }

  Serial.println("Alle MP3 bestanden verwerkt");
}

void processFile(){

  while(mp3->isRunning()){

    if(!mp3->loop()){
      mp3->stop();
    }

  }
}


void processFFT(){

  // maximaal 10 per seconde
  if(millis() - lastFFT < 100) return;
  lastFFT = millis();

  // DC offset verwijderen
  double mean = 0;
  for(int i=0;i<SAMPLES;i++) mean += vReal[i];
  mean /= SAMPLES;

  for(int i=0;i<SAMPLES;i++) vReal[i] -= mean;

  FFT.windowing(vReal,SAMPLES,FFT_WIN_TYP_HAMMING,FFT_FORWARD);
  FFT.compute(vReal,vImag,SAMPLES,FFT_FORWARD);
  FFT.complexToMagnitude(vReal,vImag,SAMPLES);

  int bands[7];
  calculateBands(bands);
  writeBands(bands);
}

void calculateBands(int bands[7]){

  for(int i=0;i<7;i++) bands[i]=0;

  // log bands
  int limits[8] = {2,4,8,16,32,64,96,128};

  for(int b=0;b<7;b++){
    for(int i=limits[b]; i<limits[b+1]; i++){
      bands[b] += vReal[i];
    }
  }

  // dynamische schaal
  static double maxVal = 1;

  for(int i=0;i<7;i++){
    if(bands[i] > maxVal) maxVal = bands[i];
  }

  for(int i=0;i<7;i++){
    bands[i] = (bands[i] / maxVal) * 15;
    if(bands[i] > 15) bands[i] = 15;
  }
}

void writeBands(int bands[7]){

  for(int i=0;i<7;i++){

    outFile.print(bands[i]);
    outFile.print(" ");

    Serial.print(bands[i]);
    Serial.print(" ");
  }

  outFile.println();
  Serial.println();
}